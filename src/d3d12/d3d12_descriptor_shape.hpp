#pragma once

#include <d3d12.h>
#include <cstdint>

namespace dxmt::d3d12 {

enum class DescriptorTextureViewShape : uint8_t {
  NotTexture,
  NonArray,
  Array,
  Unknown,
};

inline DescriptorTextureViewShape
GetSrvTextureViewShape(D3D12_SRV_DIMENSION dimension) {
  switch (dimension) {
  case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
  case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
  case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
  case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
    return DescriptorTextureViewShape::Array;
  case D3D12_SRV_DIMENSION_TEXTURE1D:
  case D3D12_SRV_DIMENSION_TEXTURE2D:
  case D3D12_SRV_DIMENSION_TEXTURE2DMS:
  case D3D12_SRV_DIMENSION_TEXTURE3D:
  case D3D12_SRV_DIMENSION_TEXTURECUBE:
    return DescriptorTextureViewShape::NonArray;
  default:
    return DescriptorTextureViewShape::NotTexture;
  }
}

inline DescriptorTextureViewShape
GetUavTextureViewShape(D3D12_UAV_DIMENSION dimension) {
  switch (dimension) {
  case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
  case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
    return DescriptorTextureViewShape::Array;
  case D3D12_UAV_DIMENSION_TEXTURE1D:
  case D3D12_UAV_DIMENSION_TEXTURE2D:
  case D3D12_UAV_DIMENSION_TEXTURE3D:
    return DescriptorTextureViewShape::NonArray;
  default:
    return DescriptorTextureViewShape::NotTexture;
  }
}

} // namespace dxmt::d3d12
