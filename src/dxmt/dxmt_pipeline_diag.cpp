#include "dxmt_pipeline_diag.hpp"

#include <algorithm>
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

std::string
BuildMetalPsoDebugLabel(std::string_view kind,
                        std::string_view shader_cache_key, size_t capacity) {
  if (capacity <= 1)
    return {};

  const size_t payload_capacity = capacity - 1;
  const size_t kind_length = std::min(kind.size(), capacity / 4);
  std::string label(kind.substr(0, kind_length));
  if (label.size() < payload_capacity)
    label.push_back(':');
  if (label.size() < payload_capacity) {
    label.append(shader_cache_key.substr(
        0, std::min(shader_cache_key.size(), payload_capacity - label.size())));
  }
  return label;
}

} // namespace dxmt
