#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace {

using dxmt::test::ComPtr;

TEST(RootSignatureSerializationSpec, Version10RoundTrip) {
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.ShaderRegister = 3;
  parameter.Constants.RegisterSpace = 2;
  parameter.Constants.Num32BitValues = 4;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC source = {};
  source.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  source.Desc_1_0.NumParameters = 1;
  source.Desc_1_0.pParameters = &parameter;
  source.Desc_1_0.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_TRUE(SUCCEEDED(D3D12SerializeVersionedRootSignature(
      &source, blob.put(), error.put())));

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_TRUE(SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(
      blob->GetBufferPointer(), blob->GetBufferSize(),
      __uuidof(ID3D12VersionedRootSignatureDeserializer),
      reinterpret_cast<void **>(deserializer.put()))));
  const auto *actual = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(actual, nullptr);
  ASSERT_EQ(actual->Version, D3D_ROOT_SIGNATURE_VERSION_1_0);
  ASSERT_EQ(actual->Desc_1_0.NumParameters, 1u);
  const auto &actual_parameter = actual->Desc_1_0.pParameters[0];
  EXPECT_EQ(actual_parameter.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
  EXPECT_EQ(actual_parameter.Constants.ShaderRegister, 3u);
  EXPECT_EQ(actual_parameter.Constants.RegisterSpace, 2u);
  EXPECT_EQ(actual_parameter.Constants.Num32BitValues, 4u);
  EXPECT_EQ(actual_parameter.ShaderVisibility,
            D3D12_SHADER_VISIBILITY_VERTEX);
  EXPECT_EQ(actual->Desc_1_0.Flags,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
}

TEST(RootSignatureSerializationSpec, Version11RoundTrip) {
  D3D12_DESCRIPTOR_RANGE1 range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 2;
  range.BaseShaderRegister = 3;
  range.RegisterSpace = 4;
  range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
  range.OffsetInDescriptorsFromTableStart = 5;
  D3D12_ROOT_PARAMETER1 parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC source = {};
  source.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  source.Desc_1_1.NumParameters = 1;
  source.Desc_1_1.pParameters = &parameter;
  source.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_TRUE(SUCCEEDED(D3D12SerializeVersionedRootSignature(
      &source, blob.put(), error.put())));

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_TRUE(SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(
      blob->GetBufferPointer(), blob->GetBufferSize(),
      __uuidof(ID3D12VersionedRootSignatureDeserializer),
      reinterpret_cast<void **>(deserializer.put()))));
  const auto *actual = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(actual, nullptr);
  ASSERT_EQ(actual->Version, D3D_ROOT_SIGNATURE_VERSION_1_1);
  ASSERT_EQ(actual->Desc_1_1.NumParameters, 1u);
  const auto &actual_parameter = actual->Desc_1_1.pParameters[0];
  ASSERT_EQ(actual_parameter.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
  ASSERT_EQ(actual_parameter.DescriptorTable.NumDescriptorRanges, 1u);
  const auto &actual_range =
      actual_parameter.DescriptorTable.pDescriptorRanges[0];
  EXPECT_EQ(actual_range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
  EXPECT_EQ(actual_range.NumDescriptors, 2u);
  EXPECT_EQ(actual_range.BaseShaderRegister, 3u);
  EXPECT_EQ(actual_range.RegisterSpace, 4u);
  EXPECT_EQ(actual_range.Flags,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE);
  EXPECT_EQ(actual_range.OffsetInDescriptorsFromTableStart, 5u);
  EXPECT_EQ(actual_parameter.ShaderVisibility,
            D3D12_SHADER_VISIBILITY_PIXEL);
}

} // namespace
