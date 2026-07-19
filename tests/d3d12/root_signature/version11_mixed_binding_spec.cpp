#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct MixedFlagCase {
  D3D12_DESCRIPTOR_RANGE_FLAGS table_flags;
  D3D12_ROOT_DESCRIPTOR_FLAGS srv_flags;
  D3D12_ROOT_DESCRIPTOR_FLAGS uav_flags;
  const char *name;
};

struct MixedSignatureDefinition {
  explicit MixedSignatureDefinition(const MixedFlagCase &test) {
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = test.table_flags;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 1;
    ranges[1].RegisterSpace = 0;
    ranges[1].Flags = static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    ranges[1].OffsetInDescriptorsFromTableStart = 1;

    parameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges =
        static_cast<UINT>(ranges.size());
    parameters[0].DescriptorTable.pDescriptorRanges = ranges.data();
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].Descriptor.RegisterSpace = 0;
    parameters[1].Descriptor.Flags = test.srv_flags;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 0;
    parameters[2].Descriptor.RegisterSpace = 0;
    parameters[2].Descriptor.Flags = test.uav_flags;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    parameters[3].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.ShaderRegister = 0;
    parameters[3].Constants.RegisterSpace = 0;
    parameters[3].Constants.Num32BitValues = 4;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
    desc.Desc_1_1.pParameters = parameters.data();
    desc.Desc_1_1.NumStaticSamplers = 1;
    desc.Desc_1_1.pStaticSamplers = &sampler;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  }

  std::array<D3D12_DESCRIPTOR_RANGE1, 2> ranges = {};
  std::array<D3D12_ROOT_PARAMETER1, 4> parameters = {};
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
};

void ExpectStaticSampler(const D3D12_STATIC_SAMPLER_DESC &sampler) {
  EXPECT_EQ(sampler.Filter, D3D12_FILTER_MIN_MAG_MIP_POINT);
  EXPECT_EQ(sampler.AddressU, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  EXPECT_EQ(sampler.AddressV, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  EXPECT_EQ(sampler.AddressW, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  EXPECT_EQ(sampler.MipLODBias, 0.0f);
  EXPECT_EQ(sampler.MaxAnisotropy, 1u);
  EXPECT_EQ(sampler.ComparisonFunc, D3D12_COMPARISON_FUNC_ALWAYS);
  EXPECT_EQ(sampler.BorderColor,
            D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK);
  EXPECT_EQ(sampler.MinLOD, 0.0f);
  EXPECT_EQ(sampler.MaxLOD, D3D12_FLOAT32_MAX);
  EXPECT_EQ(sampler.ShaderRegister, 0u);
  EXPECT_EQ(sampler.RegisterSpace, 0u);
  EXPECT_EQ(sampler.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
}

void ExpectVersion11Shape(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
    D3D12_DESCRIPTOR_RANGE_FLAGS table_flags,
    D3D12_ROOT_DESCRIPTOR_FLAGS srv_flags,
    D3D12_ROOT_DESCRIPTOR_FLAGS uav_flags) {
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->Version, D3D_ROOT_SIGNATURE_VERSION_1_1);
  const auto &root = desc->Desc_1_1;
  ASSERT_EQ(root.NumParameters, 4u);
  ASSERT_NE(root.pParameters, nullptr);
  ASSERT_EQ(root.NumStaticSamplers, 1u);
  ASSERT_NE(root.pStaticSamplers, nullptr);
  EXPECT_EQ(root.Flags, D3D12_ROOT_SIGNATURE_FLAG_NONE);

  const auto &table = root.pParameters[0];
  ASSERT_EQ(table.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
  EXPECT_EQ(table.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  ASSERT_EQ(table.DescriptorTable.NumDescriptorRanges, 2u);
  ASSERT_NE(table.DescriptorTable.pDescriptorRanges, nullptr);
  const auto &range = table.DescriptorTable.pDescriptorRanges[0];
  EXPECT_EQ(range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
  EXPECT_EQ(range.NumDescriptors, 1u);
  EXPECT_EQ(range.BaseShaderRegister, 0u);
  EXPECT_EQ(range.RegisterSpace, 0u);
  EXPECT_EQ(range.Flags, table_flags);
  EXPECT_EQ(range.OffsetInDescriptorsFromTableStart, 0u);
  const auto &output_range = table.DescriptorTable.pDescriptorRanges[1];
  EXPECT_EQ(output_range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
  EXPECT_EQ(output_range.NumDescriptors, 1u);
  EXPECT_EQ(output_range.BaseShaderRegister, 1u);
  EXPECT_EQ(output_range.RegisterSpace, 0u);
  EXPECT_EQ(output_range.Flags,
            static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE));
  EXPECT_EQ(output_range.OffsetInDescriptorsFromTableStart, 1u);

  const auto &srv = root.pParameters[1];
  ASSERT_EQ(srv.ParameterType, D3D12_ROOT_PARAMETER_TYPE_SRV);
  EXPECT_EQ(srv.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(srv.Descriptor.ShaderRegister, 1u);
  EXPECT_EQ(srv.Descriptor.RegisterSpace, 0u);
  EXPECT_EQ(srv.Descriptor.Flags, srv_flags);

  const auto &uav = root.pParameters[2];
  ASSERT_EQ(uav.ParameterType, D3D12_ROOT_PARAMETER_TYPE_UAV);
  EXPECT_EQ(uav.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(uav.Descriptor.ShaderRegister, 0u);
  EXPECT_EQ(uav.Descriptor.RegisterSpace, 0u);
  EXPECT_EQ(uav.Descriptor.Flags, uav_flags);

  const auto &constants = root.pParameters[3];
  ASSERT_EQ(constants.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
  EXPECT_EQ(constants.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(constants.Constants.ShaderRegister, 0u);
  EXPECT_EQ(constants.Constants.RegisterSpace, 0u);
  EXPECT_EQ(constants.Constants.Num32BitValues, 4u);
  ExpectStaticSampler(root.pStaticSamplers[0]);
}

void ExpectVersion10Shape(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc) {
  ASSERT_NE(desc, nullptr);
  ASSERT_EQ(desc->Version, D3D_ROOT_SIGNATURE_VERSION_1_0);
  const auto &root = desc->Desc_1_0;
  ASSERT_EQ(root.NumParameters, 4u);
  ASSERT_NE(root.pParameters, nullptr);
  ASSERT_EQ(root.NumStaticSamplers, 1u);
  ASSERT_NE(root.pStaticSamplers, nullptr);
  EXPECT_EQ(root.Flags, D3D12_ROOT_SIGNATURE_FLAG_NONE);

  const auto &table = root.pParameters[0];
  ASSERT_EQ(table.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
  EXPECT_EQ(table.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  ASSERT_EQ(table.DescriptorTable.NumDescriptorRanges, 2u);
  ASSERT_NE(table.DescriptorTable.pDescriptorRanges, nullptr);
  const auto &range = table.DescriptorTable.pDescriptorRanges[0];
  EXPECT_EQ(range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
  EXPECT_EQ(range.NumDescriptors, 1u);
  EXPECT_EQ(range.BaseShaderRegister, 0u);
  EXPECT_EQ(range.RegisterSpace, 0u);
  EXPECT_EQ(range.OffsetInDescriptorsFromTableStart, 0u);
  const auto &output_range = table.DescriptorTable.pDescriptorRanges[1];
  EXPECT_EQ(output_range.RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
  EXPECT_EQ(output_range.NumDescriptors, 1u);
  EXPECT_EQ(output_range.BaseShaderRegister, 1u);
  EXPECT_EQ(output_range.RegisterSpace, 0u);
  EXPECT_EQ(output_range.OffsetInDescriptorsFromTableStart, 1u);

  const auto &srv = root.pParameters[1];
  ASSERT_EQ(srv.ParameterType, D3D12_ROOT_PARAMETER_TYPE_SRV);
  EXPECT_EQ(srv.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(srv.Descriptor.ShaderRegister, 1u);
  EXPECT_EQ(srv.Descriptor.RegisterSpace, 0u);

  const auto &uav = root.pParameters[2];
  ASSERT_EQ(uav.ParameterType, D3D12_ROOT_PARAMETER_TYPE_UAV);
  EXPECT_EQ(uav.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(uav.Descriptor.ShaderRegister, 0u);
  EXPECT_EQ(uav.Descriptor.RegisterSpace, 0u);

  const auto &constants = root.pParameters[3];
  ASSERT_EQ(constants.ParameterType,
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
  EXPECT_EQ(constants.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
  EXPECT_EQ(constants.Constants.ShaderRegister, 0u);
  EXPECT_EQ(constants.Constants.RegisterSpace, 0u);
  EXPECT_EQ(constants.Constants.Num32BitValues, 4u);
  ExpectStaticSampler(root.pStaticSamplers[0]);
}

class RootSignatureVersion11MixedBindingSpec
    : public ::testing::TestWithParam<MixedFlagCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature = {};
    feature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(
                  D3D12_FEATURE_ROOT_SIGNATURE, &feature, sizeof(feature)),
              S_OK);
    ASSERT_EQ(feature.HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_1);

    shader_ = CompileShader(R"(
      Texture2D<float> texture_input : register(t0);
      ByteAddressBuffer buffer_input : register(t1);
      RWStructuredBuffer<uint> promised_input : register(u0);
      RWByteAddressBuffer output : register(u1);
      SamplerState point_wrap : register(s0);
      cbuffer Parameters : register(b0) {
        float2 uv;
        uint token;
        uint salt;
      };

      [numthreads(1, 1, 1)]
      void main() {
        uint sampled = asuint(
            texture_input.SampleLevel(point_wrap, uv, 0.0f));
        uint source = buffer_input.Load(0);
        uint promised = promised_input[0];
        output.Store(0, sampled);
        output.Store(4, source);
        output.Store(8, promised);
        output.Store(12, sampled ^ source ^ promised ^ token ^ salt);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();
    ASSERT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  }

  ::testing::AssertionResult ExecuteSerializedSignature(
      ID3DBlob *blob, const char *label) {
    ComPtr<ID3D12RootSignature> root_signature;
    const HRESULT root_hr = context_.device()->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),
        reinterpret_cast<void **>(root_signature.put()));
    if (root_hr != S_OK || !root_signature)
      return ::testing::AssertionFailure()
             << label << ": CreateRootSignature returned 0x" << std::hex
             << static_cast<unsigned long>(root_hr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = root_signature.get();
    pipeline_desc.CS = {shader_.bytecode->GetBufferPointer(),
                        shader_.bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT pipeline_hr = context_.device()->CreateComputePipelineState(
        &pipeline_desc, __uuidof(ID3D12PipelineState),
        reinterpret_cast<void **>(pipeline.put()));
    if (pipeline_hr != S_OK || !pipeline)
      return ::testing::AssertionFailure()
             << label << ": CreateComputePipelineState returned 0x"
             << std::hex << static_cast<unsigned long>(pipeline_hr);

    constexpr std::array<float, 4> pixels = {1.0f, 2.0f, 3.0f, 4.0f};
    constexpr UINT source_value = 0x13579bdfu;
    constexpr UINT promised_value = 0x89abcdefu;
    constexpr UINT token = 0x2468ace0u;
    constexpr UINT salt = 0x0badf00du;
    constexpr std::array<UINT, 4> constants = {
        std::bit_cast<UINT>(-0.25f), std::bit_cast<UINT>(0.25f), token, salt};
    constexpr UINT sampled = std::bit_cast<UINT>(2.0f);
    constexpr std::array<UINT, 4> expected = {
        sampled, source_value, promised_value,
        sampled ^ source_value ^ promised_value ^ token ^ salt};
    constexpr std::array<UINT, 4> poison = {
        ~expected[0], ~expected[1], ~expected[2], ~expected[3]};

    auto texture = context_.CreateTexture2D(
        2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto input = context_.CreateUploadBuffer(
        sizeof(source_value), &source_value, sizeof(source_value));
    auto promised_upload = context_.CreateUploadBuffer(
        sizeof(promised_value), &promised_value, sizeof(promised_value));
    auto poison_upload = context_.CreateUploadBuffer(
        sizeof(poison), poison.data(), sizeof(poison));
    auto promised_input = context_.CreateBuffer(
        sizeof(promised_value), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        expected.size() * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    if (!texture || !input || !promised_upload || !poison_upload ||
        !promised_input || !output || !heap)
      return ::testing::AssertionFailure()
             << label << ": failed to create execution resources";

    const HRESULT upload_hr = context_.UploadTextureAndReset(
        texture.get(), pixels.data(), 2 * sizeof(float),
        pixels.size() * sizeof(float));
    if (upload_hr != S_OK)
      return ::testing::AssertionFailure()
             << label << ": UploadTextureAndReset returned 0x" << std::hex
             << static_cast<unsigned long>(upload_hr);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    context_.device()->CreateShaderResourceView(
        texture.get(), &srv, heap->GetCPUDescriptorHandleForHeapStart());

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = static_cast<UINT>(expected.size());
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav, context_.CpuDescriptorHandle(heap.get(), 1));

    context_.list()->CopyBufferRegion(
        promised_input.get(), 0, promised_upload.get(), 0,
        sizeof(promised_value));
    context_.list()->CopyBufferRegion(output.get(), 0, poison_upload.get(), 0,
                                      sizeof(poison));
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), promised_input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> setup_bytes;
    const HRESULT setup_hr = context_.ReadbackBuffer(
        output.get(), sizeof(poison), &setup_bytes);
    if (setup_hr != S_OK || setup_bytes.size() != sizeof(poison) ||
        std::memcmp(setup_bytes.data(), poison.data(), sizeof(poison)) != 0)
      return ::testing::AssertionFailure()
             << label << ": deterministic poison setup failed";
    const HRESULT setup_reset_hr = context_.ResetCommandList();
    if (setup_reset_hr != S_OK)
      return ::testing::AssertionFailure()
             << label << ": setup ResetCommandList returned 0x" << std::hex
             << static_cast<unsigned long>(setup_reset_hr);

    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootShaderResourceView(
        1, input->GetGPUVirtualAddress());
    context_.list()->SetComputeRootUnorderedAccessView(
        2, promised_input->GetGPUVirtualAddress());
    context_.list()->SetComputeRoot32BitConstants(
        3, static_cast<UINT>(constants.size()), constants.data(), 0);
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    const HRESULT readback_hr = context_.ReadbackBuffer(
        output.get(), expected.size() * sizeof(UINT), &bytes);
    if (readback_hr != S_OK)
      return ::testing::AssertionFailure()
             << label << ": ReadbackBuffer returned 0x" << std::hex
             << static_cast<unsigned long>(readback_hr);
    const HRESULT reset_hr = context_.ResetCommandList();
    if (reset_hr != S_OK)
      return ::testing::AssertionFailure()
             << label << ": ResetCommandList returned 0x" << std::hex
             << static_cast<unsigned long>(reset_hr);
    const HRESULT device_reason =
        context_.device()->GetDeviceRemovedReason();
    if (device_reason != S_OK)
      return ::testing::AssertionFailure()
             << label << ": device removed reason was 0x" << std::hex
             << static_cast<unsigned long>(device_reason);
    if (bytes.size() != expected.size() * sizeof(UINT))
      return ::testing::AssertionFailure()
             << label << ": readback size was " << bytes.size()
             << ", expected " << expected.size() * sizeof(UINT);

    std::array<UINT, 4> actual = {};
    std::memcpy(actual.data(), bytes.data(), bytes.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (actual[index] != expected[index])
        return ::testing::AssertionFailure()
               << label << ": dword " << index << " was 0x" << std::hex
               << actual[index] << ", expected 0x" << expected[index];
    }
    return ::testing::AssertionSuccess();
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
};

TEST_P(RootSignatureVersion11MixedBindingSpec,
       SerializeDeserializeConvertAndExecuteMixedBindings) {
  const auto &test = GetParam();
  MixedSignatureDefinition source(test);
  ComPtr<ID3DBlob> version11_blob;
  ComPtr<ID3DBlob> error;
  ASSERT_EQ(D3D12SerializeVersionedRootSignature(
                &source.desc, version11_blob.put(), error.put()),
            S_OK)
      << (error ? static_cast<const char *>(error->GetBufferPointer()) : "");
  ASSERT_TRUE(version11_blob);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> deserializer;
  ASSERT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                version11_blob->GetBufferPointer(),
                version11_blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(deserializer.put())),
            S_OK);
  ASSERT_TRUE(deserializer);

  ASSERT_NO_FATAL_FAILURE(ExpectVersion11Shape(
      deserializer->GetUnconvertedRootSignatureDesc(), test.table_flags,
      test.srv_flags, test.uav_flags));

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *version11_view = nullptr;
  ASSERT_EQ(deserializer->GetRootSignatureDescAtVersion(
                D3D_ROOT_SIGNATURE_VERSION_1_1, &version11_view),
            S_OK);
  ASSERT_NO_FATAL_FAILURE(ExpectVersion11Shape(
      version11_view, test.table_flags, test.srv_flags, test.uav_flags));

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *version10_view = nullptr;
  ASSERT_EQ(deserializer->GetRootSignatureDescAtVersion(
                D3D_ROOT_SIGNATURE_VERSION_1_0, &version10_view),
            S_OK);
  ASSERT_NO_FATAL_FAILURE(ExpectVersion10Shape(version10_view));

  ComPtr<ID3DBlob> version10_blob;
  error.reset();
  ASSERT_EQ(D3D12SerializeVersionedRootSignature(
                version10_view, version10_blob.put(), error.put()),
            S_OK)
      << (error ? static_cast<const char *>(error->GetBufferPointer()) : "");
  ASSERT_TRUE(version10_blob);

  ComPtr<ID3D12VersionedRootSignatureDeserializer> version10_deserializer;
  ASSERT_EQ(D3D12CreateVersionedRootSignatureDeserializer(
                version10_blob->GetBufferPointer(),
                version10_blob->GetBufferSize(),
                __uuidof(ID3D12VersionedRootSignatureDeserializer),
                reinterpret_cast<void **>(version10_deserializer.put())),
            S_OK);
  ASSERT_TRUE(version10_deserializer);
  ASSERT_NO_FATAL_FAILURE(ExpectVersion10Shape(
      version10_deserializer->GetUnconvertedRootSignatureDesc()));

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *promoted_view = nullptr;
  ASSERT_EQ(version10_deserializer->GetRootSignatureDescAtVersion(
                D3D_ROOT_SIGNATURE_VERSION_1_1, &promoted_view),
            S_OK);
  const auto implicit_table_flags =
      static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
          D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
  ASSERT_NO_FATAL_FAILURE(ExpectVersion11Shape(
      promoted_view, implicit_table_flags,
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE));

  ASSERT_TRUE(ExecuteSerializedSignature(version11_blob.get(), "version 1.1"));
  ASSERT_TRUE(ExecuteSerializedSignature(version10_blob.get(),
                                         "converted version 1.0"));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string MixedFlagCaseName(
    const ::testing::TestParamInfo<MixedFlagCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    FlagMatrix, RootSignatureVersion11MixedBindingSpec,
    ::testing::Values(
        MixedFlagCase{
            static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE),
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            "VolatileTableStaticRoots"},
        MixedFlagCase{
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
            "StaticWhileSetTableMixedRoots"},
        MixedFlagCase{
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE,
            D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
            "StaticTableMixedRoots"}),
    MixedFlagCaseName);

} // namespace
