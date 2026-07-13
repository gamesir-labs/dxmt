#pragma once

#include "winemetal.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace dxmt {

struct ComputePipelineDiagInfo {
  uint64_t id = 0;
  std::string shader_cache_key;
};

void RegisterComputePipelineDiagInfo(obj_handle_t pso,
                                     std::string shader_cache_key);
ComputePipelineDiagInfo LookupComputePipelineDiagInfo(obj_handle_t pso);

std::string BuildMetalPsoDebugLabel(std::string_view kind,
                                    std::string_view shader_cache_key,
                                    size_t capacity);

} // namespace dxmt
