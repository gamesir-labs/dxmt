#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

enum class SamplerPublication {
  Direct,
  Copied,
  Overwritten,
};

class SamplerSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      Texture2D<float4> input : register(t0);
      SamplerState input_sampler : register(s0);
      RWTexture2D<float4> output : register(u0);
      cbuffer Parameters : register(b0) { float2 uv; };

      [numthreads(1, 1, 1)]
      void main() {
        output[uint2(0, 0)] = input.SampleLevel(input_sampler, uv, 0.0f);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE resource_ranges[2] = {};
    resource_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    resource_ranges[0].NumDescriptors = 1;
    resource_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    resource_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    resource_ranges[1].NumDescriptors = 1;
    resource_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_DESCRIPTOR_RANGE sampler_range = {};
    sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    sampler_range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    parameters[0].DescriptorTable.pDescriptorRanges = resource_ranges;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &sampler_range;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants.ShaderRegister = 0;
    parameters[2].Constants.Num32BitValues = 2;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);

    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(), shader_.bytecode->GetBufferSize()};
    pipeline_ =
        context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  void RunCase(D3D12_FILTER filter, float u, float v,
               const std::array<float, 4> &expected,
               SamplerPublication publication) {
    const std::array<std::array<float, 4>, 4> pixels = {{
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    }};
    auto input = context_.CreateTexture2D(
        2, 2, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateTexture2D(
        1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto resource_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    auto sampler_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(resource_heap);
    ASSERT_TRUE(sampler_heap);
    ASSERT_EQ(context_.UploadTextureAndReset(
                  input.get(), pixels.data(), 2 * sizeof(pixels[0]),
                  sizeof(pixels)),
              S_OK);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    context_.device()->CreateShaderResourceView(
        input.get(), &srv,
        context_.CpuDescriptorHandle(resource_heap.get(), 0));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, nullptr,
        context_.CpuDescriptorHandle(resource_heap.get(), 1));

    D3D12_SAMPLER_DESC sampler = {};
    sampler.Filter = filter;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    if (publication == SamplerPublication::Copied) {
      auto cpu_heap = context_.CreateDescriptorHeap(
          D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, false);
      ASSERT_TRUE(cpu_heap);
      context_.device()->CreateSampler(
          &sampler, cpu_heap->GetCPUDescriptorHandleForHeapStart());
      context_.device()->CopyDescriptorsSimple(
          1, sampler_heap->GetCPUDescriptorHandleForHeapStart(),
          cpu_heap->GetCPUDescriptorHandleForHeapStart(),
          D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    } else {
      if (publication == SamplerPublication::Overwritten) {
        auto decoy = sampler;
        decoy.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        context_.device()->CreateSampler(
            &decoy, sampler_heap->GetCPUDescriptorHandleForHeapStart());
      }
      context_.device()->CreateSampler(
          &sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());
    }

    D3D12TestContext::Transition(
        context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
    context_.list()->SetDescriptorHeaps(2, heaps);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline_.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, resource_heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootDescriptorTable(
        1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
    const UINT coordinates[] = {
        std::bit_cast<UINT>(u), std::bit_cast<UINT>(v)};
    context_.list()->SetComputeRoot32BitConstants(2, 2, coordinates, 0);
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(output.get(), &readback), S_OK);
    std::array<float, 4> actual = {};
    std::memcpy(actual.data(), readback.data.data(), sizeof(actual));
    for (UINT component = 0; component < actual.size(); ++component)
      EXPECT_NEAR(actual[component], expected[component], 1e-6f)
          << "component " << component;
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_F(SamplerSpec, PointSamplerSamplesTexelCenter) {
  RunCase(D3D12_FILTER_MIN_MAG_MIP_POINT, 0.25f, 0.25f,
          {1.0f, 0.0f, 0.0f, 1.0f}, SamplerPublication::Direct);
}

TEST_F(SamplerSpec, LinearSamplerAveragesFourTexels) {
  RunCase(D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0.5f, 0.5f,
          {0.5f, 0.5f, 0.5f, 1.0f}, SamplerPublication::Direct);
}

TEST_F(SamplerSpec, CopiedSamplerMatchesOriginal) {
  RunCase(D3D12_FILTER_MIN_MAG_MIP_POINT, 0.25f, 0.25f,
          {1.0f, 0.0f, 0.0f, 1.0f}, SamplerPublication::Copied);
}

TEST_F(SamplerSpec, OverwrittenSamplerUsesLatestFilter) {
  RunCase(D3D12_FILTER_MIN_MAG_MIP_LINEAR, 0.5f, 0.5f,
          {0.5f, 0.5f, 0.5f, 1.0f}, SamplerPublication::Overwritten);
}

} // namespace
