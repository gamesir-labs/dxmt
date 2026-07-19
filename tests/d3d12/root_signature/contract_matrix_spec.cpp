#include <dxmt_test.hpp>
#include <dxmt_test_com.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
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
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
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
  EXPECT_EQ(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                        blob.put(), error.put()),
            test.expected);
  EXPECT_EQ(bool(blob), SUCCEEDED(test.expected));
}

std::string
RootCostCaseName(const ::testing::TestParamInfo<RootCostCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    BoundaryMatrix, RootSignatureCostMatrixSpec,
    ::testing::Values(
        RootCostCase{RootCostShape::Constants, 1, S_OK, "Constants1"},
        RootCostCase{RootCostShape::Constants, 63, S_OK, "Constants63"},
        RootCostCase{RootCostShape::Constants, 64, S_OK, "Constants64"},
        RootCostCase{RootCostShape::Constants, 65, E_INVALIDARG, "Constants65"},
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
  const D3D12_ROOT_SIGNATURE_DESC desc = {static_cast<UINT>(parameters.size()),
                                          parameters.data(), 0, nullptr,
                                          D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> error;
  return D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                     blob, error.put());
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

TEST(RootSignatureLayoutMatrixSpec, ExplicitSameTypeTableSlotAliasIsAccepted) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0, 1};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureLayoutMatrixSpec,
     ExplicitDifferentTypeTableSlotOverlapIsRejected) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 1};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), E_INVALIDARG);
  EXPECT_FALSE(blob);
}

TEST(RootSignatureLayoutMatrixSpec,
     UnboundedRangeAtNonzeroRegisterAndTableOffsetRoundTrips) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = UINT_MAX;
  range.BaseShaderRegister = 8;
  range.OffsetInDescriptorsFromTableStart = 8;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {1, &range};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;
  ASSERT_EQ(SerializeLayout(parameters, blob.put()), S_OK);
  ASSERT_TRUE(blob);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                blob->GetBufferPointer(), blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);
  const auto *desc = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->Version, D3D_ROOT_SIGNATURE_VERSION_1_0);
  ASSERT_EQ(desc->Desc_1_0.NumParameters, 1u);
  const auto &decoded =
      desc->Desc_1_0.pParameters[0].DescriptorTable.pDescriptorRanges[0];
  EXPECT_EQ(decoded.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
  EXPECT_EQ(decoded.NumDescriptors, UINT_MAX);
  EXPECT_EQ(decoded.BaseShaderRegister, 8u);
  EXPECT_EQ(decoded.OffsetInDescriptorsFromTableStart, 8u);
}

TEST(RootSignatureLayoutMatrixSpec,
     RangeAfterUnboundedRequiresExplicitTableOffset) {
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 8, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1, 32};
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable = {2, ranges};
  std::vector<D3D12_ROOT_PARAMETER> parameters = {parameter};
  ComPtr<ID3DBlob> blob;

  EXPECT_EQ(SerializeLayout(parameters, blob.put()), S_OK);
  EXPECT_TRUE(blob);

  ranges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  blob.reset();
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
  const D3D12_ROOT_SIGNATURE_DESC desc = {1, &parameter, 1, &sampler,
                                          D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;

  EXPECT_EQ(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                        blob.put(), error.put()),
            S_OK);
  EXPECT_TRUE(blob);
}

TEST(RootSignatureMalformedBlobSpec, EveryTruncationIsRejectedAndClearsOutput) {
  const D3D12_ROOT_SIGNATURE_DESC desc = {};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  ASSERT_EQ(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                        blob.put(), error.put()),
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

std::uint32_t ReadBlobU32(const std::vector<std::uint8_t> &blob,
                          std::size_t offset) {
  return std::uint32_t(blob[offset + 0]) |
         (std::uint32_t(blob[offset + 1]) << 8) |
         (std::uint32_t(blob[offset + 2]) << 16) |
         (std::uint32_t(blob[offset + 3]) << 24);
}

void WriteBlobU32(std::vector<std::uint8_t> &blob, std::size_t offset,
                  std::uint32_t value) {
  blob[offset + 0] = std::uint8_t(value & 0xff);
  blob[offset + 1] = std::uint8_t((value >> 8) & 0xff);
  blob[offset + 2] = std::uint8_t((value >> 16) & 0xff);
  blob[offset + 3] = std::uint8_t((value >> 24) & 0xff);
}

std::vector<std::uint8_t> SerializeStructuredRootSignature() {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 2;
  range.BaseShaderRegister = 3;
  range.RegisterSpace = 1;
  range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 7;
  sampler.RegisterSpace = 2;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  const D3D12_ROOT_SIGNATURE_DESC desc = {
      1, &parameter, 1, &sampler,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         blob.put(), error.put())) ||
      !blob)
    return {};
  const auto *begin =
      static_cast<const std::uint8_t *>(blob->GetBufferPointer());
  return {begin, begin + blob->GetBufferSize()};
}

std::vector<std::uint8_t> SerializeVersion11DescriptorTables() {
  D3D12_DESCRIPTOR_RANGE1 range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 2;
  range.BaseShaderRegister = 3;
  range.RegisterSpace = 1;
  range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
  range.OffsetInDescriptorsFromTableStart = 0;

  std::array<D3D12_ROOT_PARAMETER1, 2> parameters = {};
  for (auto &parameter : parameters) {
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;
  }
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  desc.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
  desc.Desc_1_1.pParameters = parameters.data();
  ComPtr<ID3DBlob> blob;
  ComPtr<ID3DBlob> error;
  if (FAILED(D3D12SerializeVersionedRootSignature(&desc, blob.put(),
                                                  error.put())) ||
      !blob)
    return {};
  const auto *begin =
      static_cast<const std::uint8_t *>(blob->GetBufferPointer());
  return {begin, begin + blob->GetBufferSize()};
}

::testing::AssertionResult
MalformedBlobIsRejected(const std::vector<std::uint8_t> &blob) {
  void *versioned = reinterpret_cast<void *>(std::uintptr_t{1});
  const HRESULT versioned_result =
      D3D12CreateVersionedRootSignatureDeserializer(
          blob.data(), blob.size(),
          __uuidof(ID3D12VersionedRootSignatureDeserializer), &versioned);
  if (versioned_result != E_INVALIDARG || versioned != nullptr) {
    if (SUCCEEDED(versioned_result) && versioned)
      static_cast<IUnknown *>(versioned)->Release();
    return ::testing::AssertionFailure()
           << "versioned result=" << std::hex << versioned_result
           << " output=" << versioned;
  }

  void *legacy = reinterpret_cast<void *>(std::uintptr_t{1});
  const HRESULT legacy_result = D3D12CreateRootSignatureDeserializer(
      blob.data(), blob.size(), __uuidof(ID3D12RootSignatureDeserializer),
      &legacy);
  if (legacy_result != E_INVALIDARG || legacy != nullptr) {
    if (SUCCEEDED(legacy_result) && legacy)
      static_cast<IUnknown *>(legacy)->Release();
    return ::testing::AssertionFailure()
           << "legacy result=" << std::hex << legacy_result
           << " output=" << legacy;
  }
  return ::testing::AssertionSuccess();
}

TEST(RootSignatureMalformedBlobSpec,
     Version11ParameterVisibilityCorruptionIsRejected) {
  auto blob = SerializeVersion11DescriptorTables();
  ASSERT_GE(blob.size(), 44u);
  const std::size_t part_header = ReadBlobU32(blob, 32);
  ASSERT_LE(part_header + 8, blob.size());
  const std::size_t rts0 = part_header + 8;
  ASSERT_EQ(ReadBlobU32(blob, rts0), 2u);
  const std::size_t parameter_headers = rts0 + ReadBlobU32(blob, rts0 + 8);
  ASSERT_LE(parameter_headers + 12, blob.size());

  WriteBlobU32(blob, parameter_headers + 4,
               std::numeric_limits<std::uint32_t>::max());
  EXPECT_TRUE(MalformedBlobIsRejected(blob));
}

TEST(RootSignatureMalformedBlobSpec,
     ExactAliasedParameterPayloadAndRangeIsAcceptedAndQueryable) {
  auto blob = SerializeVersion11DescriptorTables();
  ASSERT_GE(blob.size(), 44u);
  const std::size_t part_header = ReadBlobU32(blob, 32);
  ASSERT_LE(part_header + 8, blob.size());
  const std::size_t rts0 = part_header + 8;
  ASSERT_EQ(ReadBlobU32(blob, rts0), 2u);
  ASSERT_EQ(ReadBlobU32(blob, rts0 + 4), 2u);
  const std::size_t parameter_headers = rts0 + ReadBlobU32(blob, rts0 + 8);
  ASSERT_LE(parameter_headers + 24, blob.size());
  const std::uint32_t first_payload = ReadBlobU32(blob, parameter_headers + 8);
  ASSERT_NE(ReadBlobU32(blob, parameter_headers + 20), first_payload);

  WriteBlobU32(blob, parameter_headers + 20, first_payload);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                blob.data(), blob.size(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);
  const auto *desc = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->Version, D3D_ROOT_SIGNATURE_VERSION_1_1);
  ASSERT_EQ(desc->Desc_1_1.NumParameters, 2u);
  for (UINT index = 0; index < 2; ++index) {
    const auto &parameter = desc->Desc_1_1.pParameters[index];
    ASSERT_EQ(parameter.ParameterType,
              D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    ASSERT_EQ(parameter.DescriptorTable.NumDescriptorRanges, 1u);
    const auto &range = parameter.DescriptorTable.pDescriptorRanges[0];
    EXPECT_EQ(range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    EXPECT_EQ(range.NumDescriptors, 2u);
    EXPECT_EQ(range.BaseShaderRegister, 3u);
    EXPECT_EQ(range.RegisterSpace, 1u);
  }
  EXPECT_EQ(desc->Desc_1_1.pParameters[0].ShaderVisibility,
            D3D12_SHADER_VISIBILITY_VERTEX);
  EXPECT_EQ(desc->Desc_1_1.pParameters[1].ShaderVisibility,
            D3D12_SHADER_VISIBILITY_PIXEL);
}

TEST(RootSignatureMalformedBlobSpec,
     ValidBlobWithTrailingDataPreservesDecodedDescription) {
  auto blob = SerializeStructuredRootSignature();
  ASSERT_FALSE(blob.empty());
  const auto original_size = blob.size();
  blob.insert(blob.end(), 32, 0xa5);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                blob.data(), blob.size(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);
  const auto *desc = deserializer->GetUnconvertedRootSignatureDesc();
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->Version, D3D_ROOT_SIGNATURE_VERSION_1_0);
  EXPECT_EQ(desc->Desc_1_0.NumParameters, 1u);
  EXPECT_EQ(desc->Desc_1_0.NumStaticSamplers, 1u);
  EXPECT_EQ(ReadBlobU32(blob, 24), original_size);
}

} // namespace
