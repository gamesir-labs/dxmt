#include "d3d12_sampler.hpp"

#include <algorithm>

namespace dxmt::d3d12 {

namespace {

static WMTSamplerAddressMode
AddressMode(D3D12_TEXTURE_ADDRESS_MODE mode) {
  switch (mode) {
  case D3D12_TEXTURE_ADDRESS_MODE_MIRROR:
    return WMTSamplerAddressModeMirrorRepeat;
  case D3D12_TEXTURE_ADDRESS_MODE_CLAMP:
    return WMTSamplerAddressModeClampToEdge;
  case D3D12_TEXTURE_ADDRESS_MODE_BORDER:
    return WMTSamplerAddressModeClampToBorderColor;
  case D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE:
    return WMTSamplerAddressModeMirrorClampToEdge;
  default:
    return WMTSamplerAddressModeRepeat;
  }
}

static WMTCompareFunction
CompareFunction(D3D12_COMPARISON_FUNC func) {
  switch (func) {
  case D3D12_COMPARISON_FUNC_LESS:
    return WMTCompareFunctionLess;
  case D3D12_COMPARISON_FUNC_EQUAL:
    return WMTCompareFunctionEqual;
  case D3D12_COMPARISON_FUNC_LESS_EQUAL:
    return WMTCompareFunctionLessEqual;
  case D3D12_COMPARISON_FUNC_GREATER:
    return WMTCompareFunctionGreater;
  case D3D12_COMPARISON_FUNC_NOT_EQUAL:
    return WMTCompareFunctionNotEqual;
  case D3D12_COMPARISON_FUNC_GREATER_EQUAL:
    return WMTCompareFunctionGreaterEqual;
  case D3D12_COMPARISON_FUNC_ALWAYS:
    return WMTCompareFunctionAlways;
  default:
    return WMTCompareFunctionNever;
  }
}

static WMTSamplerBorderColor
BorderColor(const FLOAT color[4]) {
  if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f &&
      color[3] == 0.0f)
    return WMTSamplerBorderColorTransparentBlack;
  if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f &&
      color[3] == 1.0f)
    return WMTSamplerBorderColorOpaqueBlack;
  return WMTSamplerBorderColorOpaqueWhite;
}

} // namespace

Rc<Sampler>
CreateD3D12Sampler(WMT::Device device, const D3D12_SAMPLER_DESC &desc) {
  WMTSamplerInfo info = {};
  info.lod_average = false;
  info.min_filter = D3D12_DECODE_MIN_FILTER(desc.Filter)
                        ? WMTSamplerMinMagFilterLinear
                        : WMTSamplerMinMagFilterNearest;
  info.mag_filter = D3D12_DECODE_MAG_FILTER(desc.Filter)
                        ? WMTSamplerMinMagFilterLinear
                        : WMTSamplerMinMagFilterNearest;
  info.mip_filter = D3D12_DECODE_MIP_FILTER(desc.Filter)
                        ? WMTSamplerMipFilterLinear
                        : WMTSamplerMipFilterNearest;
  info.lod_min_clamp = desc.MinLOD;
  info.lod_max_clamp = std::max(desc.MinLOD, desc.MaxLOD);
  info.max_anisotroy = D3D12_DECODE_IS_ANISOTROPIC_FILTER(desc.Filter)
                           ? std::clamp<UINT>(desc.MaxAnisotropy, 1, 16)
                           : 1;
  info.s_address_mode = AddressMode(desc.AddressU);
  info.t_address_mode = AddressMode(desc.AddressV);
  info.r_address_mode = AddressMode(desc.AddressW);
  info.compare_function = WMTCompareFunctionNever;
  if (D3D12_DECODE_IS_COMPARISON_FILTER(desc.Filter))
    info.compare_function = CompareFunction(desc.ComparisonFunc);
  info.border_color = BorderColor(desc.BorderColor);
  info.support_argument_buffers = true;
  info.normalized_coords = true;
  return Sampler::createSampler(device, info, desc.MipLODBias);
}

Rc<Sampler>
CreateD3D12StaticSampler(WMT::Device device,
                         const D3D12_STATIC_SAMPLER_DESC &desc) {
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = desc.Filter;
  sampler.AddressU = desc.AddressU;
  sampler.AddressV = desc.AddressV;
  sampler.AddressW = desc.AddressW;
  sampler.MipLODBias = desc.MipLODBias;
  sampler.MaxAnisotropy = desc.MaxAnisotropy;
  sampler.ComparisonFunc = desc.ComparisonFunc;
  sampler.MinLOD = desc.MinLOD;
  sampler.MaxLOD = desc.MaxLOD;
  switch (desc.BorderColor) {
  case D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK:
    sampler.BorderColor[3] = 1.0f;
    break;
  case D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE:
    sampler.BorderColor[0] = 1.0f;
    sampler.BorderColor[1] = 1.0f;
    sampler.BorderColor[2] = 1.0f;
    sampler.BorderColor[3] = 1.0f;
    break;
  case D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK:
  default:
    break;
  }
  return CreateD3D12Sampler(device, sampler);
}

} // namespace dxmt::d3d12
