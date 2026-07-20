#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#include "d3d12_test_context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstdint>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

TEST(RootSignatureLayoutSpec, Exactly64DwordsAccepted) {
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;

  EXPECT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureLayoutSpec, Exactly65DwordsRejectedByDevice) {
  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(), S_OK);
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST + 1;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;

  ASSERT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            S_OK);
  ASSERT_TRUE(blob);

  void *root_signature = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(context.device()->CreateRootSignature(
                0, blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12RootSignature), &root_signature),
            E_INVALIDARG);
  EXPECT_EQ(root_signature, nullptr);
  if (root_signature)
    static_cast<ID3D12RootSignature *>(root_signature)->Release();
}

} // namespace
