#pragma once

#include "com/com_pointer.hpp"
#include "d3d12_device.hpp"
#include <d3d12.h>
#include <cstddef>
#include <span>
#include <vector>

namespace dxmt::d3d12 {

struct RootSignatureRange {
  D3D12_DESCRIPTOR_RANGE_TYPE range_type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  UINT base_shader_register = 0;
  UINT register_space = 0;
  UINT descriptor_count = 0;
  UINT offset_in_descriptors_from_table_start = 0;
  D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
};

struct RootSignatureParameter {
  D3D12_ROOT_PARAMETER_TYPE parameter_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
  std::vector<RootSignatureRange> ranges;
  D3D12_ROOT_DESCRIPTOR1 descriptor = {};
  D3D12_ROOT_CONSTANTS constants = {};
  D3D12_ROOT_DESCRIPTOR_FLAGS descriptor_flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
};

class RootSignature {
public:
  virtual ~RootSignature() = default;

  // Monotonic per-process identity for caches whose entries can cross submit
  // batches. Pointer addresses alone are not safe because COM objects may be
  // destroyed and later allocated at the same address.
  virtual uint64_t GetCacheIdentity() const = 0;
  virtual ULONG STDMETHODCALLTYPE AddRefPrivate() = 0;
  virtual void STDMETHODCALLTYPE ReleasePrivate() = 0;
  virtual IMTLD3D12Device *GetParentDevice() const = 0;
  virtual const D3D12_VERSIONED_ROOT_SIGNATURE_DESC &GetVersionedDesc() const = 0;
  virtual std::span<const std::byte> GetSerializedBlob() const = 0;
  virtual std::span<const RootSignatureParameter> GetParameters() const = 0;
  virtual std::span<const D3D12_STATIC_SAMPLER_DESC> GetStaticSamplers() const = 0;
};

// Private IID for a non-RTTI downcast ID3D12RootSignature* -> dxmt RootSignature*.
// Internal fast-path only; QueryInterface returns a borrowed pointer without AddRef.
inline constexpr GUID IID_DXMTRootSignatureDowncast = {
    0x4bb5e4d2, 0x26d5, 0x4f8b,
    {0xa1, 0xdb, 0x31, 0xa4, 0x71, 0x96, 0x42, 0x5e}};

RootSignature *GetDXMTRootSignature(ID3D12RootSignature *root_signature);

Com<ID3D12RootSignature> CreateRootSignatureFromBlob(IMTLD3D12Device *device,
                                                      std::span<const std::byte> blob);

HRESULT CreateRootSignatureDeserializer(std::span<const std::byte> blob,
                                         REFIID iid, void **deserializer);

HRESULT CreateRootSignatureDeserializerFromSubobjectInLibrary(
    std::span<const std::byte> library_blob, const WCHAR *subobject_name,
    REFIID iid, void **deserializer);

HRESULT SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *root_signature_desc,
    ID3DBlob **blob, ID3DBlob **error_blob);

} // namespace dxmt::d3d12
