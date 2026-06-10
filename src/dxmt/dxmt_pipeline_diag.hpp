#pragma once

#include "winemetal.h"
#include <cstdint>
#include <string>

namespace dxmt {

struct ComputePipelineDiagInfo {
  uint64_t id = 0;
  std::string shader_cache_key;
};

void RegisterComputePipelineDiagInfo(obj_handle_t pso,
                                     std::string shader_cache_key);
ComputePipelineDiagInfo LookupComputePipelineDiagInfo(obj_handle_t pso);

} // namespace dxmt
