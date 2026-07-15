#pragma once

#include <d3d12.h>

namespace dxmt::test {

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
struct alignas(void *) ShaderPipelineSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  D3D12_SHADER_BYTECODE shader = {};
};

} // namespace dxmt::test
