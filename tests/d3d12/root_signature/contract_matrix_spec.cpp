#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;

enum class RootCostShape { Constants, RootCbv, DescriptorTable };

struct RootCostCase {
  RootCostShape shape;
  UINT count;
  HRESULT expected;
  const char *name;
};

class RootSignatureCostMatrixSpec
    : public ::testing::TestWithParam<RootCostCase> {};

TEST_P(RootSignatureCostMatrixSpec, EnforcesDwordCostBoundary) {
  const auto &test = GetParam();
  std::vector<D3D12_ROOT_PARAMETER> parameters;
  std::vector<D3D12_DESCRIPTOR_RANGE> ranges;

  if (test.shape == RootCostShape::Constants) {
    parameters.resize(1);
    parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = test.count;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  } else if (test.shape == RootCostShape::RootCbv) {
    parameters.resize(test.count);
    for (UINT index = 0; index < test.count; ++index) {
      parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameters[index].Descriptor.ShaderRegister = index;
      parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
  } else {
    parameters.resize(test.count);
    ranges.resize(test.count);
    for (UINT index = 0; index < test.count; ++index) {
      ranges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
      ranges[index].NumDescriptors = 1;
      ranges[index].BaseShaderRegister = index;
      ranges[index].OffsetInDescriptorsFromTableStart = 0;
      parameters[index].ParameterType =
          D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameters[index].DescriptorTable.NumDescriptorRanges = 1;
      parameters[index].DescriptorTable.pDescriptorRanges = &ranges[index];
      parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
  }

  const D3D12_ROOT_SIGNATURE_DESC desc = {
      static_cast<UINT>(parameters.size()),
      parameters.empty() ? nullptr : parameters.data(), 0, nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  EXPECT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            test.expected);
  EXPECT_EQ(bool(blob), SUCCEEDED(test.expected));
}

std::string RootCostCaseName(
    const ::testing::TestParamInfo<RootCostCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryMatrix, RootSignatureCostMatrixSpec,
    ::testing::Values(
        RootCostCase{RootCostShape::Constants, 1, S_OK, "Constants1"},
        RootCostCase{RootCostShape::Constants, 63, S_OK, "Constants63"},
        RootCostCase{RootCostShape::Constants, 64, S_OK, "Constants64"},
        RootCostCase{RootCostShape::Constants, 65, E_INVALIDARG,
                     "Constants65"},
        RootCostCase{RootCostShape::RootCbv, 1, S_OK, "RootCbv2Dwords"},
        RootCostCase{RootCostShape::RootCbv, 31, S_OK, "RootCbv62Dwords"},
        RootCostCase{RootCostShape::RootCbv, 32, S_OK, "RootCbv64Dwords"},
        RootCostCase{RootCostShape::RootCbv, 33, E_INVALIDARG,
                     "RootCbv66Dwords"},
        RootCostCase{RootCostShape::DescriptorTable, 1, S_OK, "Tables1"},
        RootCostCase{RootCostShape::DescriptorTable, 63, S_OK, "Tables63"},
        RootCostCase{RootCostShape::DescriptorTable, 64, S_OK, "Tables64"},
        RootCostCase{RootCostShape::DescriptorTable, 65, E_INVALIDARG,
                     "Tables65"}),
    RootCostCaseName);

HRESULT SerializeLayout(const std::vector<D3D12_ROOT_PARAMETER> &parameters,
                        ID3DBlob **blob) {
  const D3D12_ROOT_SIGNATURE_DESC desc = {
      static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
      D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> error;
  return D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob, error.put());
}

TEST(RootSignatureLayoutMatrixSpec, AppendRangesDoNotOverlap) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0,
               D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureLayoutMatrixSpec, ExplicitTableSlotOverlapIsRejected) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0, 1};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), E_INVALIDARG);
  EXPECT_FALSE(blob);
}

TEST(RootSignatureLayoutMatrixSpec,
     SameRegisterAndIntersectingVisibilityIsRejected) {
  std::vector<D3D12_ROOT_PARAMETER> parameters(2);
  for (auto &parameter : parameters) {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 3;
    parameter.Descriptor.RegisterSpace = 2;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), E_INVALIDARG);
  EXPECT_FALSE(blob);
}

TEST(RootSignatureLayoutMatrixSpec,
     SameRegisterWithDisjointVisibilityIsAccepted) {
  std::vector<D3D12_ROOT_PARAMETER> parameters(2);
  for (auto &parameter : parameters) {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister = 3;
    parameter.Descriptor.RegisterSpace = 2;
  }
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureLayoutMatrixSpec, StaticSamplerDoesNotConsumeRootCost) {
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST;
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  const D3D12_ROOT_SIGNATURE_DESC desc = {
      1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;

  EXPECT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureMalformedBlobSpec, EveryTruncationIsRejectedAndClearsOutput) {
  const D3D12_ROOT_SIGNATURE_DESC desc = {};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_EQ(D3D12SerializeRootSignature(
                &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(), error.put()),
            S_OK);
  ASSERT_TRUE(blob);
  for (SIZE_T size = 0; size < blob->GetBufferSize(); ++size) {
    void *output = reinterpret_cast<void *>(std::uintptr_t{1});
    EXPECT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                  blob->GetBufferPointer(), size,
                  __uuidof(ID3D12VersionedRootSignatureDeserializer), &output),
              E_INVALIDARG)
        << "truncated size " << size;
    EXPECT_EQ(output, nullptr) << "truncated size " << size;
  }
}

} // namespace
