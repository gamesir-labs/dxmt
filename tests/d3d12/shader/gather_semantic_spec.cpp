#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12GatherSemanticSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature> CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE resources[2] = {};
    resources[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    resources[0].NumDescriptors = 1;
    resources[0].OffsetInDescriptorsFromTableStart = 0;
    resources[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    resources[1].NumDescriptors = 1;
    resources[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_DESCRIPTOR_RANGE samplers = {};
    samplers.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplers.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    parameters[0].DescriptorTable.pDescriptorRanges = resources;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &samplers;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    return context_.CreateRootSignature(desc);
  }

  static D3D12_SAMPLER_DESC PointSampler(bool comparison) {
    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT
                             : D3D12_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = comparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                                     : D3D12_COMPARISON_FUNC_ALWAYS;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    return desc;
  }

  std::vector<float> Run(const char *source, DXGI_FORMAT format, UINT width,
                         UINT height, const void *pixels, UINT row_pitch,
                         UINT slice_pitch, UINT output_count,
                         bool comparison) {
    const auto shader = CompileShader(source, "cs_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    auto root = CreateRootSignature();
    EXPECT_TRUE(root);
    if (!root)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
    auto texture = context_.CreateTexture2D(
        width, height, 1, format, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        output_count * sizeof(float), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto resources = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    auto samplers = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    EXPECT_TRUE(pipeline);
    EXPECT_TRUE(texture);
    EXPECT_TRUE(output);
    EXPECT_TRUE(resources);
    EXPECT_TRUE(samplers);
    if (!pipeline || !texture || !output || !resources || !samplers)
      return {};
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), pixels, row_pitch,
                                             slice_pitch),
              S_OK);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = output_count;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        texture.get(), &srv, context_.CpuDescriptorHandle(resources.get(), 0));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(resources.get(), 1));
    const auto sampler = PointSampler(comparison);
    context_.device()->CreateSampler(
        &sampler, samplers->GetCPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
    context_.list()->SetDescriptorHeaps(2, heaps);
    context_.list()->SetComputeRootSignature(root.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, resources->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootDescriptorTable(
        1, samplers->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), output_count * sizeof(float),
                                      &bytes),
              S_OK);
    if (bytes.size() != output_count * sizeof(float))
      return {};
    std::vector<float> values(output_count);
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
  }

  D3D12TestContext context_;
};

TEST_F(D3D12GatherSemanticSpec, ChannelsAndImmediateOffsetSelectExpectedTexels) {
  constexpr char kShader[] = R"(
    Texture2D<float4> input : register(t0);
    SamplerState input_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      float4 red = input.GatherRed(input_sampler, float2(0.5f, 0.5f));
      float4 green = input.GatherGreen(input_sampler, float2(0.5f, 0.5f));
      float4 blue = input.GatherBlue(input_sampler, float2(0.5f, 0.5f));
      float4 alpha = input.GatherAlpha(input_sampler, float2(0.5f, 0.5f));
      float4 sums = float4(dot(red, 1.0f), dot(green, 1.0f),
                           dot(blue, 1.0f), dot(alpha, 1.0f));
      float offset_sum = dot(input.GatherRed(
          input_sampler, float2(0.5f, 0.5f), int2(1, 0)), 1.0f);
      output.Store4(0, asuint(sums));
      output.Store(16, asuint(offset_sum));
    }
  )";
  constexpr std::array<float, 16> kPixels = {
      1.0f, 10.0f, 100.0f, 1000.0f, 2.0f, 20.0f, 200.0f, 2000.0f,
      3.0f, 30.0f, 300.0f, 3000.0f, 4.0f, 40.0f, 400.0f, 4000.0f};
  const auto values =
      Run(kShader, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 2, kPixels.data(),
          2 * 4 * sizeof(float), sizeof(kPixels), 5, false);
  ASSERT_EQ(values.size(), 5u);
  EXPECT_FLOAT_EQ(values[0], 10.0f);
  EXPECT_FLOAT_EQ(values[1], 100.0f);
  EXPECT_FLOAT_EQ(values[2], 1000.0f);
  EXPECT_FLOAT_EQ(values[3], 10000.0f);
  EXPECT_FLOAT_EQ(values[4], 12.0f);
}

TEST_F(D3D12GatherSemanticSpec,
       ComparisonAndProgrammableOffsetProducePerTapResults) {
  constexpr char kShader[] = R"(
    Texture2D<float> input : register(t0);
    SamplerComparisonState input_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
      float4 base = input.GatherCmp(
          input_sampler, float2(0.5f, 0.5f), 0.75f);
      int2 runtime_offset = int2(id.x + 1, id.y);
      float4 offset = input.GatherCmp(
          input_sampler, float2(0.5f, 0.5f), 0.75f, runtime_offset);
      output.Store4(0, asuint(base));
      output.Store4(16, asuint(offset));
    }
  )";
  constexpr std::array<float, 4> kPixels = {0.1f, 0.6f, 0.4f, 0.9f};
  const auto values = Run(kShader, DXGI_FORMAT_R32_FLOAT, 2, 2,
                          kPixels.data(), 2 * sizeof(float), sizeof(kPixels), 8,
                          true);
  ASSERT_EQ(values.size(), 8u);
  EXPECT_EQ(values,
            (std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f,
                                1.0f, 1.0f, 0.0f, 0.0f}));
}

} // namespace
