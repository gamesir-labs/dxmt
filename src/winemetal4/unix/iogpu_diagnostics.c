#include "iogpu_diagnostics.h"

#include <execinfo.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

typedef id (*IOGPUErrorInitializer)(id, SEL, int64_t, signed char);
typedef uint64_t (*IOGPUFaultAddressGetter)(id, SEL);

enum { IOGPU_ERROR_INNOCENT_VICTIM = 5 };

static IOGPUErrorInitializer original_error_initializer;
static IOGPUFaultAddressGetter original_fault_address_getter;
static pthread_mutex_t install_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool error_hook_installed;
static int diagnostic_fd = -1;
static _Thread_local bool inside_error_hook;
static _Thread_local bool inside_fault_hook;

static void
write_event_header(const char *event) {
  struct timespec now = {0};
  uint64_t thread_id = 0;
  clock_gettime(CLOCK_REALTIME, &now);
  pthread_threadid_np(NULL, &thread_id);
  dprintf(diagnostic_fd,
          "\nDXMT_IOGPU_NATIVE event=%s pid=%d tid=%llu time=%lld.%09ld\n",
          event, getpid(), thread_id, (long long)now.tv_sec, now.tv_nsec);
}

static void
write_native_stack(void) {
  size_t stack_size = pthread_get_stacksize_np(pthread_self());
  if (!stack_size) {
    dprintf(diagnostic_fd, "backtrace_error=stack_size_unavailable\n");
    return;
  }
  size_t capacity = stack_size / sizeof(void *);
  if (capacity > INT_MAX)
    capacity = INT_MAX;

  void **frames = calloc(capacity, sizeof(*frames));
  if (!frames) {
    dprintf(diagnostic_fd,
            "backtrace_error=allocation_failed capacity=%zu\n", capacity);
    return;
  }

  int count = backtrace(frames, (int)capacity);
  dprintf(diagnostic_fd, "backtrace_frames=%d capacity=%zu\n", count,
          capacity);
  backtrace_symbols_fd(frames, count, diagnostic_fd);
  free(frames);
}

static void
begin_event(const char *event) {
  pthread_mutex_lock(&log_lock);
  flock(diagnostic_fd, LOCK_EX);
  write_event_header(event);
}

static void
end_event(void) {
  fsync(diagnostic_fd);
  flock(diagnostic_fd, LOCK_UN);
  pthread_mutex_unlock(&log_lock);
}

static id
capture_iogpu_error(id self, SEL selector, int64_t io_gpu_error,
                    signed char mtl4_queue_error) {
  if (inside_error_hook || io_gpu_error == IOGPU_ERROR_INNOCENT_VICTIM)
    return original_error_initializer(self, selector, io_gpu_error,
                                      mtl4_queue_error);

  inside_error_hook = true;
  begin_event("iogpu-error-entry");
  dprintf(diagnostic_fd,
          "self=%p selector=%s ioGPUError=%lld mtl4QueueError=%d\n", self,
          sel_getName(selector), (long long)io_gpu_error,
          (int)mtl4_queue_error);
  write_native_stack();
  end_event();

  id result = original_error_initializer(self, selector, io_gpu_error,
                                         mtl4_queue_error);

  begin_event("iogpu-error-return");
  dprintf(diagnostic_fd,
          "result=%p ioGPUError=%lld mtl4QueueError=%d\n", result,
          (long long)io_gpu_error, (int)mtl4_queue_error);
  write_native_stack();
  end_event();
  inside_error_hook = false;
  return result;
}

static uint64_t
capture_fault_address(id self, SEL selector) {
  if (inside_fault_hook)
    return original_fault_address_getter(self, selector);

  inside_fault_hook = true;
  uint64_t address = original_fault_address_getter(self, selector);
  begin_event("iogpu-fault-address");
  dprintf(diagnostic_fd, "self=%p selector=%s address=0x%llx\n", self,
          sel_getName(selector), (unsigned long long)address);
  write_native_stack();
  end_event();
  inside_fault_hook = false;
  return address;
}

static const char *
skip_objc_type_qualifiers(const char *type) {
  while (type && *type && strchr("rnNoORV", *type))
    type++;
  return type;
}

static bool
error_initializer_abi_is_supported(Method method) {
  char return_type[16] = {0};
  char error_type[16] = {0};
  char queue_error_type[16] = {0};
  method_getReturnType(method, return_type, sizeof(return_type));
  method_getArgumentType(method, 2, error_type, sizeof(error_type));
  method_getArgumentType(method, 3, queue_error_type,
                         sizeof(queue_error_type));
  const char *error_base = skip_objc_type_qualifiers(error_type);
  const char *queue_error_base = skip_objc_type_qualifiers(queue_error_type);
  return method_getNumberOfArguments(method) == 4 && return_type[0] == '@' &&
         error_base && strchr("qQ", error_base[0]) && queue_error_base &&
         strchr("cCB", queue_error_base[0]);
}

static void
open_diagnostic_log(void) {
  if (diagnostic_fd >= 0)
    return;

  const char *directory = getenv("DXMT_LOG_PATH");
  if (!directory || !directory[0]) {
    fprintf(stderr,
            "err:   DXMT IOGPU native diagnostics disabled: DXMT_LOG_PATH is unset\n");
    return;
  }

  char path[PATH_MAX];
  int length = snprintf(path, sizeof(path), "%s/dxmt-iogpu-native.log",
                        directory);
  if (length <= 0 || (size_t)length >= sizeof(path)) {
    fprintf(stderr,
            "err:   DXMT IOGPU native diagnostics disabled: log path is too long\n");
    return;
  }

  diagnostic_fd =
      open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (diagnostic_fd < 0) {
    fprintf(stderr,
            "err:   DXMT IOGPU native diagnostics disabled: cannot open %s: %s\n",
            path, strerror(errno));
    return;
  }

  begin_event("diagnostics-loaded");
  dprintf(diagnostic_fd,
          "architecture=x86_64 log=%s ignoredIOGPUError=%d\n", path,
          IOGPU_ERROR_INNOCENT_VICTIM);
  end_event();
}

static void
install_optional_fault_address_hook(void) {
  Class command_buffer_class = objc_getClass("IOGPUMetalCommandBuffer");
  SEL selector = sel_registerName("gpuFaultAddress");
  Method method = command_buffer_class
                      ? class_getInstanceMethod(command_buffer_class, selector)
                      : NULL;
  if (!method || method_getNumberOfArguments(method) != 2)
    return;

  original_fault_address_getter =
      (IOGPUFaultAddressGetter)method_setImplementation(
          method, (IMP)capture_fault_address);
  begin_event("iogpu-fault-hook-installed");
  dprintf(diagnostic_fd, "type=%s original=%p replacement=%p\n",
          method_getTypeEncoding(method), original_fault_address_getter,
          capture_fault_address);
  end_event();
}

void
dxmt_iogpu_diagnostics_install(void) {
  if (atomic_load_explicit(&error_hook_installed, memory_order_acquire))
    return;

  pthread_mutex_lock(&install_lock);
  if (atomic_load_explicit(&error_hook_installed, memory_order_relaxed)) {
    pthread_mutex_unlock(&install_lock);
    return;
  }

  open_diagnostic_log();
  if (diagnostic_fd < 0) {
    pthread_mutex_unlock(&install_lock);
    return;
  }

  Class error_class = objc_getClass("NSError");
  SEL selector = sel_registerName("initWithIOGPUError:MTL4QueueError:");
  Method method = error_class ? class_getInstanceMethod(error_class, selector)
                              : NULL;
  if (!method) {
    begin_event("iogpu-error-hook-unavailable");
    dprintf(diagnostic_fd, "selector=%s\n", sel_getName(selector));
    end_event();
    pthread_mutex_unlock(&install_lock);
    return;
  }

  if (!error_initializer_abi_is_supported(method)) {
    begin_event("iogpu-error-hook-rejected");
    dprintf(diagnostic_fd, "type=%s arguments=%u\n",
            method_getTypeEncoding(method),
            method_getNumberOfArguments(method));
    end_event();
    pthread_mutex_unlock(&install_lock);
    return;
  }

  original_error_initializer = (IOGPUErrorInitializer)method_setImplementation(
      method, (IMP)capture_iogpu_error);
  atomic_store_explicit(&error_hook_installed, true, memory_order_release);

  begin_event("iogpu-error-hook-installed");
  dprintf(diagnostic_fd, "type=%s original=%p replacement=%p\n",
          method_getTypeEncoding(method), original_error_initializer,
          capture_iogpu_error);
  end_event();
  install_optional_fault_address_hook();
  pthread_mutex_unlock(&install_lock);
}
