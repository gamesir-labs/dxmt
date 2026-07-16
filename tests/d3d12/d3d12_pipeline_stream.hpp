#pragma once

#include <d3d12.h>

namespace dxmt::test {

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename Payload>
struct alignas(void *) PipelineSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  Payload value = {};
};

} // namespace dxmt::test
