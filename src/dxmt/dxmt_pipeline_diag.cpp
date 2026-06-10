#include "dxmt_pipeline_diag.hpp"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace dxmt {
namespace {

std::mutex &
ComputePipelineDiagMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<obj_handle_t, ComputePipelineDiagInfo> &
ComputePipelineDiagMap() {
  static std::unordered_map<obj_handle_t, ComputePipelineDiagInfo> map;
  return map;
}

uint64_t
NextComputePipelineDiagId() {
  static std::atomic<uint64_t> id = 1;
  return id.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

void
RegisterComputePipelineDiagInfo(obj_handle_t pso, std::string shader_cache_key) {
  if (!pso)
    return;

  std::lock_guard lock(ComputePipelineDiagMutex());
  auto &entry = ComputePipelineDiagMap()[pso];
  if (!entry.id)
    entry.id = NextComputePipelineDiagId();
  entry.shader_cache_key = std::move(shader_cache_key);
}

ComputePipelineDiagInfo
LookupComputePipelineDiagInfo(obj_handle_t pso) {
  if (!pso)
    return {};

  std::lock_guard lock(ComputePipelineDiagMutex());
  auto &map = ComputePipelineDiagMap();
  auto entry = map.find(pso);
  return entry == map.end() ? ComputePipelineDiagInfo{} : entry->second;
}

} // namespace dxmt
