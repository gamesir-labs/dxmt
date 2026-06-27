#include "dxmt_apitrace.hpp"

#include "log/log.hpp"
#include "util_env.hpp"
#include "winemetal.h"

#ifdef DXMT_APITRACE_D3D
#include "apitrace/capture_runtime.hpp"
#endif

#include <atomic>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#if DXMT_DX12_METAL4
#define DXMT_WMT_APITRACE_SESSION_FLUSH WMT4ApitraceSessionFlush
#define DXMT_WMT_APITRACE_SESSION_ENSURE_OPEN WMT4ApitraceSessionEnsureOpen
#define DXMT_WMT_APITRACE_SESSION_SEAL_CHECKPOINT WMT4ApitraceSessionSealCheckpoint
#define DXMT_WMT_APITRACE_SESSION_CLOSE WMT4ApitraceSessionClose
#define DXMT_WMT_APITRACE_SET_CURRENT_D3D_SEQUENCE WMT4ApitraceSetCurrentD3DSequence
#define DXMT_WMT_APITRACE_COMMAND_BUFFER_BEGIN WMT4ApitraceCommandBufferBegin
#define DXMT_WMT_APITRACE_COMMAND_BUFFER_COMMIT WMT4ApitraceCommandBufferCommit
#define DXMT_WMT_APITRACE_PRESENT_DRAWABLE WMT4ApitracePresentDrawable
#else
#define DXMT_WMT_APITRACE_SESSION_FLUSH WMTApitraceSessionFlush
#define DXMT_WMT_APITRACE_SESSION_ENSURE_OPEN WMTApitraceSessionEnsureOpen
#define DXMT_WMT_APITRACE_SESSION_SEAL_CHECKPOINT WMTApitraceSessionSealCheckpoint
#define DXMT_WMT_APITRACE_SESSION_CLOSE WMTApitraceSessionClose
#define DXMT_WMT_APITRACE_SET_CURRENT_D3D_SEQUENCE WMTApitraceSetCurrentD3DSequence
#define DXMT_WMT_APITRACE_COMMAND_BUFFER_BEGIN WMTApitraceCommandBufferBegin
#define DXMT_WMT_APITRACE_COMMAND_BUFFER_COMMIT WMTApitraceCommandBufferCommit
#define DXMT_WMT_APITRACE_PRESENT_DRAWABLE WMTApitracePresentDrawable
#endif

namespace dxmt::apitrace {
namespace {

std::atomic<int> enabled_cache{-1};
std::atomic_bool session_open_logged = false;
std::atomic_bool shutdown_requested = false;
#ifdef _WIN32
std::atomic_bool crash_flush_handler_installed = false;
std::atomic_bool crash_flush_running = false;
#endif

constexpr const char *kTraceBundleEnv = "APITRACE_TRACE_BUNDLE";

bool
truthy_env_value(const std::string &value) {
  return value == "1" || value == "true" || value == "yes" || value == "trace";
}

bool
env_enabled() {
  const int cached = enabled_cache.load(std::memory_order_relaxed);
  if (cached >= 0)
    return cached != 0;

  auto value = env::getEnvVar("DXMT_APITRACE_ENABLED");
  const bool enabled = truthy_env_value(value);
  enabled_cache.store(enabled ? 1 : 0, std::memory_order_relaxed);
  return enabled;
}

bool
ends_with(const std::string &value, const char *suffix) {
  const std::string suffix_string(suffix);
  return value.size() >= suffix_string.size() &&
         value.compare(value.size() - suffix_string.size(), suffix_string.size(), suffix_string) == 0;
}

void
set_env_var(const char *name, const char *value) {
#ifdef _WIN32
  SetEnvironmentVariableA(name, value);
  // SetEnvironmentVariableA only updates the PEB; mingw msvcrt keeps its own
  // environ table for std::getenv. apitrace's resolve_bundle_root reads via
  // std::getenv, so without the CRT-side write the PE TraceSession would fall
  // back to the cwd-default bundle even after we mirror the value here.
  _putenv_s(name, value);

  using WineSetUnixEnvProc = LONG(WINAPI *)(const char *, const char *);
  auto set_unix_env = reinterpret_cast<WineSetUnixEnvProc>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "__wine_set_unix_env"));
  if (set_unix_env)
    set_unix_env(name, value);
#else
  setenv(name, value, 1);
#endif
}

std::string
default_bundle_root() {
  const auto base_dir = env::getUnixPath(env::getExeBaseName() + "_dxmt_apitrace");
  if (base_dir.empty())
    return {};

  env::createDirectory(base_dir);
  std::time_t now;
  std::time(&now);
  char timestamp[32] = {};
  std::strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%S", std::localtime(&now));
  return base_dir + "/trace-" + timestamp + ".apitrace";
}

std::string
bundle_root_from_env(const std::string &value) {
  if (value.empty())
    return {};

  const auto unix_path = env::getUnixPath(value);
  if (unix_path.empty())
    return {};

  if (ends_with(unix_path, ".apitrace"))
    return unix_path;

  WARN("DXMT apitrace: ignoring APITRACE_TRACE_BUNDLE because it is not a .apitrace bundle: ", unix_path.c_str());
  return {};
}

void
initialize_bundle_root() {
  auto trace_bundle = bundle_root_from_env(env::getEnvVar(kTraceBundleEnv));

  if (trace_bundle.empty())
    trace_bundle = default_bundle_root();

  if (!trace_bundle.empty())
    set_env_var(kTraceBundleEnv, trace_bundle.c_str());
}

#ifdef _WIN32
bool
is_debug_exception(DWORD code) {
  constexpr DWORD kThreadNameException = 0x406D1388;
  return code == DBG_PRINTEXCEPTION_C ||
         code == DBG_PRINTEXCEPTION_WIDE_C ||
         code == DBG_CONTROL_C ||
         code == DBG_CONTROL_BREAK ||
         code == kThreadNameException;
}

void
flush_sessions_for_crash() {
  if (!enabled())
    return;

  if (shutdown_requested.load(std::memory_order_acquire))
    return;

  if (crash_flush_running.exchange(true, std::memory_order_acq_rel))
    return;

#ifdef DXMT_APITRACE_D3D
  ::apitrace::runtime::flush_process_trace_session();
#endif
  DXMT_WMT_APITRACE_SESSION_FLUSH();
  crash_flush_running.store(false, std::memory_order_release);
}

LONG WINAPI
apitrace_crash_flush_handler(EXCEPTION_POINTERS *exception_info) {
  if (!exception_info || !exception_info->ExceptionRecord)
    return EXCEPTION_CONTINUE_SEARCH;

  const DWORD code = exception_info->ExceptionRecord->ExceptionCode;
  if (is_debug_exception(code))
    return EXCEPTION_CONTINUE_SEARCH;

  flush_sessions_for_crash();
  return EXCEPTION_CONTINUE_SEARCH;
}

void
install_crash_flush_handler() {
  if (crash_flush_handler_installed.exchange(true, std::memory_order_acq_rel))
    return;

  AddVectoredExceptionHandler(1, apitrace_crash_flush_handler);
}
#endif

} // namespace

bool
enabled() {
  return env_enabled();
}

void
ensure_session_open() {
  if (!enabled())
    return;

  if (shutdown_requested.load(std::memory_order_acquire))
    return;

  static std::atomic_bool bundle_initialized = false;
  if (!bundle_initialized.exchange(true, std::memory_order_relaxed))
    initialize_bundle_root();

#ifdef _WIN32
  install_crash_flush_handler();
#endif

  DXMT_WMT_APITRACE_SESSION_ENSURE_OPEN();
  if (!session_open_logged.exchange(true, std::memory_order_relaxed))
    INFO("DXMT apitrace: session open requested");
}

void
seal_checkpoint() {
  if (!enabled())
    return;

  if (shutdown_requested.load(std::memory_order_acquire))
    return;

  DXMT_WMT_APITRACE_SESSION_SEAL_CHECKPOINT();
#ifdef DXMT_APITRACE_D3D
  ::apitrace::runtime::seal_process_trace_session_checkpoint();
#endif
}

void
seal_metal_checkpoint() {
  if (!enabled())
    return;

  if (shutdown_requested.load(std::memory_order_acquire))
    return;

  DXMT_WMT_APITRACE_SESSION_SEAL_CHECKPOINT();
}

void
seal_d3d_checkpoint() {
  if (!enabled())
    return;

  if (shutdown_requested.load(std::memory_order_acquire))
    return;

#ifdef DXMT_APITRACE_D3D
  ::apitrace::runtime::seal_process_trace_session_checkpoint();
#endif
}

void
shutdown() {
  if (!enabled())
    return;

  if (shutdown_requested.exchange(true, std::memory_order_acq_rel))
    return;

  DXMT_WMT_APITRACE_SESSION_CLOSE();
#ifdef DXMT_APITRACE_D3D
  ::apitrace::runtime::shutdown_process_trace_session();
#endif
}

void
set_current_d3d_sequence(uint64_t d3d_sequence) {
  if (!enabled())
    return;

  ensure_session_open();
  DXMT_WMT_APITRACE_SET_CURRENT_D3D_SEQUENCE(d3d_sequence);
}

void
on_command_buffer_begin(uint64_t command_buffer_id, uint64_t frame_id) {
  if (!enabled())
    return;

  ensure_session_open();
  DXMT_WMT_APITRACE_COMMAND_BUFFER_BEGIN(command_buffer_id, frame_id);
}

void
on_command_buffer_commit(uint64_t command_buffer_id) {
  if (!enabled())
    return;

  DXMT_WMT_APITRACE_COMMAND_BUFFER_COMMIT(command_buffer_id);
}

void
on_present_drawable(
    uint64_t command_buffer_id,
    uint64_t drawable_id,
    uint64_t frame_index,
    uint32_t sync_interval,
    uint32_t flags) {
  if (!enabled())
    return;

  DXMT_WMT_APITRACE_PRESENT_DRAWABLE(command_buffer_id, drawable_id, frame_index, sync_interval, flags);
}

} // namespace dxmt::apitrace
