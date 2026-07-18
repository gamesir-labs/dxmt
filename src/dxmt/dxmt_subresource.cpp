#include "dxmt_subresource.hpp"
#include "dxmt_texture.hpp"

namespace dxmt {

unsigned
getPlanarCount(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatX32G8X32:
  case WMTPixelFormatR32X8X32:
  case WMTPixelFormatDepth32Float_Stencil8:
  case WMTPixelFormatDepth24Unorm_Stencil8:
  case WMTPixelFormatX32_Stencil8:
  case WMTPixelFormatX24_Stencil8:
    return 2;
  default:
    break;
  }
  return 1;
}

unsigned
getPlanarMask(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatX32G8X32:
    return 0b10;
  case WMTPixelFormatR32X8X32:
    return 0b01;
  case WMTPixelFormatDepth32Float_Stencil8:
  case WMTPixelFormatDepth24Unorm_Stencil8:
    return 0b11;
  case WMTPixelFormatX32_Stencil8:
  case WMTPixelFormatX24_Stencil8:
    return 0b10;
  default:
    break;
  }
  return 1;
}

ResourceSubsetState::ResourceSubsetState(
    const TextureViewDescriptor *desc, uint32_t total_mip_count, uint32_t total_array_size, uint32_t ignore_planar_mask
) {
  auto total_planar = getPlanarCount(desc->format);
  auto planar_mask = getPlanarMask(desc->format) & ~ignore_planar_mask;
  const uint64_t mip_end =
      uint64_t(desc->firstMiplevel) + desc->miplevelCount;
  const uint64_t array_end =
      uint64_t(desc->firstArraySlice) + desc->arraySize;
  if (mip_end > total_mip_count || array_end > total_array_size) {
    encoded_tag = 0;
    return;
  }

  const uint64_t total_subresources = uint64_t(total_planar) *
                                      total_mip_count * total_array_size;
  if (total_subresources <= 62) {
    encoded_tag = 0b11;
    uint64_t bits = 0;
    for (auto planar = 0u; planar < total_planar; planar++) {
      // unsigned int bit-fields promote to int if narrower than int
      for (auto slice = desc->firstArraySlice; slice < unsigned(desc->firstArraySlice + desc->arraySize); slice++) {
        for (auto level = desc->firstMiplevel; level < unsigned(desc->firstMiplevel + desc->miplevelCount); level++) {
          if ((1 << planar) & planar_mask)
            bits |= 1ull << (planar * total_array_size * total_mip_count + slice * total_mip_count + level);
        }
      }
    }
    texture_bitmask.mask = bits;
  } else {
    if (desc->firstMiplevel > 31 || mip_end > 31 ||
        desc->firstArraySlice > 4095 || array_end > 4095) {
      encoded_tag = 0;
      return;
    }
    encoded_tag = 0b10;

    texture.mip_start = desc->firstMiplevel;
    texture.mip_end = mip_end;
    texture.array_start = desc->firstArraySlice;
    texture.array_end = array_end;
    texture.planar_mask = planar_mask;
  }
}

} // namespace dxmt
