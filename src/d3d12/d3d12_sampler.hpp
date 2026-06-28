#pragma once

#include "Metal.hpp"
#include "dxmt_sampler.hpp"
#include <d3d12.h>

namespace dxmt::d3d12 {

Rc<Sampler> CreateD3D12Sampler(WMT::Device device,
                               const D3D12_SAMPLER_DESC &desc);

Rc<Sampler> CreateD3D12StaticSampler(
    WMT::Device device, const D3D12_STATIC_SAMPLER_DESC &desc);

} // namespace dxmt::d3d12
