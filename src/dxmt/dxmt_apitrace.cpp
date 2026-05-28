#include "dxmt_apitrace.hpp"

#include "log/log.hpp"
#include "util_env.hpp"
#include "winemetal.h"

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <string>

namespace dxmt::apitrace {
namespace {

std::atomic<int> enabled_cache{-1};
std::atomic<int> verbose_cache{-1};
std::atomic_bool session_open_logged = false;

bool
truthy_env_value(const std::string &value) {
  return value == "1" || value == "true" || value == "yes" || value == "trace";
}

bool
env_enabled() {
  const int cached = enabled_cache.load(std::memory_order_relaxed);
  if (cached >= 0)
    return cached != 0;

  auto value = env::getEnvVar("DXMT_APITRACE_ENBALED");
  if (value.empty())
    value = env::getEnvVar("DXMT_APITRACE_ENABLED");
  const bool enabled = truthy_env_value(value);
  enabled_cache.store(enabled ? 1 : 0, std::memory_order_relaxed);
  return enabled;
}

bool
verbose_enabled() {
  const int cached = verbose_cache.load(std::memory_order_relaxed);
  if (cached >= 0)
    return cached != 0;

  const auto value = env::getEnvVar("APITRACE_METAL_VERBOSE");
  const bool verbose = truthy_env_value(value);
  verbose_cache.store(verbose ? 1 : 0, std::memory_order_relaxed);
  return verbose;
}

void
log_verbose(const char *message, uint64_t arg0 = 0, uint64_t arg1 = 0) {
  if (!verbose_enabled())
    return;

  INFO("DXMT apitrace: ", message, " arg0=", arg0, " arg1=", arg1);
}

} // namespace

bool
enabled() {
  return env_enabled();
}

void
ensure_session_open() {
  if (!enabled())
    return;

  static std::atomic_bool bundle_initialized = false;
  if (!bundle_initialized.exchange(true, std::memory_order_relaxed) && env::getEnvVar("APITRACE_METAL_BUNDLE").empty()) {
    const auto base_dir = env::getUnixPath(env::getExeBaseName() + "_dxmt_apitrace");
    if (!base_dir.empty()) {
      env::createDirectory(base_dir);
      std::time_t now;
      std::time(&now);
      char timestamp[32] = {};
      std::strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%S", std::localtime(&now));
      const auto bundle_root = base_dir + "/trace-" + timestamp + ".apitrace";
      setenv("APITRACE_METAL_BUNDLE", bundle_root.c_str(), 1);
      if (verbose_enabled()) {
        INFO("DXMT apitrace: bundle root ", bundle_root);
      }
    }
  }

  WMTApitraceSessionEnsureOpen();
  if (!session_open_logged.exchange(true, std::memory_order_relaxed) && verbose_enabled()) {
    INFO("DXMT apitrace: session open requested");
  }
}

void
shutdown() {
  if (!enabled())
    return;

  WMTApitraceSessionClose();
  log_verbose("session close");
}

void
set_current_d3d_sequence(uint64_t d3d_sequence) {
  if (!enabled())
    return;

  WMTApitraceSetCurrentD3DSequence(d3d_sequence);
}

void
on_command_buffer_begin(uint64_t command_buffer_id, uint64_t frame_id) {
  if (!enabled())
    return;

  ensure_session_open();
  WMTApitraceCommandBufferBegin(command_buffer_id, frame_id);
  log_verbose("command buffer begin", command_buffer_id, frame_id);
}

void
on_command_buffer_commit(uint64_t command_buffer_id) {
  if (!enabled())
    return;

  WMTApitraceCommandBufferCommit(command_buffer_id);
  log_verbose("command buffer commit", command_buffer_id, 0);
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

  WMTApitracePresentDrawable(command_buffer_id, drawable_id, frame_index, sync_interval, flags);
  if (verbose_enabled()) {
    INFO("DXMT apitrace: present commandBuffer=", command_buffer_id,
         " drawable=", drawable_id,
         " frame=", frame_index,
         " syncInterval=", sync_interval,
         " flags=", flags);
  }
}

} // namespace dxmt::apitrace
