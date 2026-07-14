#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace {

using dxmt::test::ComPtr;

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

TEST(RootSignatureLayoutSpec, Exactly65DwordsRejected) {
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST + 1;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;

  EXPECT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            E_INVALIDARG);
  EXPECT_FALSE(blob);
}

} // namespace
