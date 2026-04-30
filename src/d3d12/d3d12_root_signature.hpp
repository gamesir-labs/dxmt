#pragma once

#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include <d3d12.h>
#include <cstddef>
#include <span>

namespace dxmt::d3d12 {

class RootSignature {
public:
  virtual ~RootSignature() = default;

  virtual const D3D12_VERSIONED_ROOT_SIGNATURE_DESC &GetVersionedDesc() const = 0;
  virtual std::span<const std::byte> GetSerializedBlob() const = 0;
};

Com<ID3D12RootSignature> CreateRootSignatureFromBlob(IMTLD3D12Device *device,
                                                      std::span<const std::byte> blob);

} // namespace dxmt::d3d12
