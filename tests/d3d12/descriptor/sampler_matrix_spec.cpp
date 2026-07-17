#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class SamplerMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    sample_shader_ = CompileShader(R"(
      Texture2D<float> input : register(t0);
      SamplerState input_sampler : register(s0);
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        float2 uv;
        float lod;
        uint output_index;
      };

      [numthreads(1, 1, 1)]
      void main() {
        output.Store(output_index * 4,
                     asuint(input.SampleLevel(input_sampler, uv, lod)));
      }
    )", "cs_5_0");
    comparison_shader_ = CompileShader(R"(
      Texture2D<float> input : register(t0);
      SamplerComparisonState input_sampler : register(s0);
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        float2 uv;
        float reference;
        uint output_index;
      };

      [numthreads(1, 1, 1)]
      void main() {
        output.Store(output_index * 4, asuint(
            input.SampleCmpLevelZero(input_sampler, uv, reference)));
      }
    )", "cs_5_0");
    gradient_shader_ = CompileShader(R"(
      Texture2D<float> input : register(t0);
      SamplerState input_sampler : register(s0);
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        float2 uv;
        float gradient;
        uint output_index;
      };

      [numthreads(1, 1, 1)]
      void main() {
        output.Store(output_index * 4, asuint(input.SampleGrad(
            input_sampler, uv, float2(gradient, 0.0f),
            float2(0.0f, gradient))));
      }
    )", "cs_5_0");
    ASSERT_EQ(sample_shader_.result, S_OK) << sample_shader_.diagnostic_text();
    ASSERT_EQ(comparison_shader_.result, S_OK)
        << comparison_shader_.diagnostic_text();
    ASSERT_EQ(gradient_shader_.result, S_OK)
        << gradient_shader_.diagnostic_text();
    CreateDynamicPipeline(sample_shader_, &sample_root_, &sample_pipeline_);
    CreateDynamicPipeline(comparison_shader_, &comparison_root_,
                          &comparison_pipeline_);
    CreateDynamicPipeline(gradient_shader_, &gradient_root_,
                          &gradient_pipeline_);
  }

  void CreateDynamicPipeline(const dxmt::test::ShaderCompilation &shader,
                             ComPtr<ID3D12RootSignature> *root,
                             ComPtr<ID3D12PipelineState> *pipeline) {
    D3D12_DESCRIPTOR_RANGE resources[2] = {};
    resources[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    resources[0].NumDescriptors = 1;
    resources[0].OffsetInDescriptorsFromTableStart = 0;
    resources[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    resources[1].NumDescriptors = 1;
    resources[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_DESCRIPTOR_RANGE sampler = {};
    sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    sampler.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    parameters[0].DescriptorTable.pDescriptorRanges = resources;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &sampler;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[2].Constants.ShaderRegister = 0;
    parameters[2].Constants.Num32BitValues = 4;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    *root = context_.CreateRootSignature(desc);
    ASSERT_TRUE(*root);
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    *pipeline = context_.CreateComputePipeline(root->get(), bytecode);
    ASSERT_TRUE(*pipeline);
  }

  static D3D12_SAMPLER_DESC PointSampler(
      D3D12_TEXTURE_ADDRESS_MODE address =
          D3D12_TEXTURE_ADDRESS_MODE_CLAMP) {
    D3D12_SAMPLER_DESC desc = {};
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = address;
    desc.AddressV = address;
    desc.AddressW = address;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = D3D12_FLOAT32_MAX;
    return desc;
  }

  ComPtr<ID3D12Resource> CreateOutput(UINT element_count) {
    return context_.CreateBuffer(
        element_count * sizeof(float), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  void CreateViews(ID3D12Resource *texture, UINT mip_levels,
                   ID3D12Resource *output, UINT output_elements,
                   ID3D12DescriptorHeap *heap) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = mip_levels;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = output_elements;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateShaderResourceView(
        texture, &srv, context_.CpuDescriptorHandle(heap, 0));
    context_.device()->CreateUnorderedAccessView(
        output, nullptr, &uav, context_.CpuDescriptorHandle(heap, 1));
  }

  float RunSingleSample(const D3D12_SAMPLER_DESC &sampler, float u, float v,
                        float lod, bool comparison = false,
                        float reference = 0.0f) {
    constexpr std::array<float, 4> pixels = {1.0f, 2.0f, 3.0f, 4.0f};
    auto texture = context_.CreateTexture2D(
        2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = CreateOutput(1);
    auto resources = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    auto samplers = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    EXPECT_TRUE(texture);
    EXPECT_TRUE(output);
    EXPECT_TRUE(resources);
    EXPECT_TRUE(samplers);
    if (!texture || !output || !resources || !samplers)
      return std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(context_.UploadTextureAndReset(
                  texture.get(), pixels.data(), 2 * sizeof(float),
                  pixels.size() * sizeof(float)),
              S_OK);
    CreateViews(texture.get(), 1, output.get(), 1, resources.get());
    context_.device()->CreateSampler(
        &sampler, samplers->GetCPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
    context_.list()->SetDescriptorHeaps(2, heaps);
    const auto *root = comparison ? comparison_root_.get() : sample_root_.get();
    const auto *pipeline =
        comparison ? comparison_pipeline_.get() : sample_pipeline_.get();
    context_.list()->SetComputeRootSignature(
        const_cast<ID3D12RootSignature *>(root));
    context_.list()->SetPipelineState(
        const_cast<ID3D12PipelineState *>(pipeline));
    context_.list()->SetComputeRootDescriptorTable(
        0, resources->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootDescriptorTable(
        1, samplers->GetGPUDescriptorHandleForHeapStart());
    const UINT constants[] = {
        std::bit_cast<UINT>(u), std::bit_cast<UINT>(v),
        std::bit_cast<UINT>(comparison ? reference : lod), 0};
    context_.list()->SetComputeRoot32BitConstants(2, 4, constants, 0);
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), sizeof(float), &bytes),
              S_OK);
    float result = std::numeric_limits<float>::quiet_NaN();
    if (bytes.size() >= sizeof(result))
      std::memcpy(&result, bytes.data(), sizeof(result));
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    return result;
  }

  float RunMipSampleWithPipeline(const D3D12_SAMPLER_DESC &sampler,
                                 float parameter,
                                 ID3D12RootSignature *root,
                                 ID3D12PipelineState *pipeline) {
    std::array<float, 16> mip0 = {};
    std::array<float, 4> mip1 = {};
    std::array<float, 1> mip2 = {};
    mip0.fill(1.0f);
    mip1.fill(2.0f);
    mip2.fill(4.0f);
    auto texture = context_.CreateTexture2D(
        4, 4, 3, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = CreateOutput(1);
    auto resources = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    auto samplers = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    EXPECT_TRUE(texture);
    EXPECT_TRUE(output);
    EXPECT_TRUE(resources);
    EXPECT_TRUE(samplers);
    if (!texture || !output || !resources || !samplers)
      return std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip0.data(),
                                             4 * sizeof(float), sizeof(mip0), 0),
              S_OK);
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip1.data(),
                                             2 * sizeof(float), sizeof(mip1), 1),
              S_OK);
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip2.data(),
                                             sizeof(float), sizeof(mip2), 2),
              S_OK);
    CreateViews(texture.get(), 3, output.get(), 1, resources.get());
    context_.device()->CreateSampler(
        &sampler, samplers->GetCPUDescriptorHandleForHeapStart());
    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[] = {resources.get(), samplers.get()};
    context_.list()->SetDescriptorHeaps(2, heaps);
    context_.list()->SetComputeRootSignature(root);
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, resources->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetComputeRootDescriptorTable(
        1, samplers->GetGPUDescriptorHandleForHeapStart());
    const UINT constants[] = {std::bit_cast<UINT>(0.5f),
                              std::bit_cast<UINT>(0.5f),
                              std::bit_cast<UINT>(parameter), 0};
    context_.list()->SetComputeRoot32BitConstants(2, 4, constants, 0);
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.get(), sizeof(float), &bytes),
              S_OK);
    float result = std::numeric_limits<float>::quiet_NaN();
    if (bytes.size() >= sizeof(result))
      std::memcpy(&result, bytes.data(), sizeof(result));
    EXPECT_EQ(context_.ResetCommandList(), S_OK);
    return result;
  }

  float RunMipSample(const D3D12_SAMPLER_DESC &sampler, float lod) {
    return RunMipSampleWithPipeline(sampler, lod, sample_root_.get(),
                                    sample_pipeline_.get());
  }

  float RunMipGradientSample(const D3D12_SAMPLER_DESC &sampler,
                             float gradient) {
    return RunMipSampleWithPipeline(sampler, gradient, gradient_root_.get(),
                                    gradient_pipeline_.get());
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation sample_shader_;
  dxmt::test::ShaderCompilation comparison_shader_;
  dxmt::test::ShaderCompilation gradient_shader_;
  ComPtr<ID3D12RootSignature> sample_root_;
  ComPtr<ID3D12PipelineState> sample_pipeline_;
  ComPtr<ID3D12RootSignature> comparison_root_;
  ComPtr<ID3D12PipelineState> comparison_pipeline_;
  ComPtr<ID3D12RootSignature> gradient_root_;
  ComPtr<ID3D12PipelineState> gradient_pipeline_;
};

TEST_F(SamplerMatrixSpec, AddressModesSelectExpectedTexelsOrBorder) {
  struct Case {
    D3D12_TEXTURE_ADDRESS_MODE mode;
    float expected;
  };
  constexpr std::array cases = {
      Case{D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1.0f},
      Case{D3D12_TEXTURE_ADDRESS_MODE_WRAP, 2.0f},
      Case{D3D12_TEXTURE_ADDRESS_MODE_MIRROR, 1.0f},
      Case{D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(static_cast<UINT>(test.mode));
    const auto sampler = PointSampler(test.mode);
    EXPECT_NEAR(RunSingleSample(sampler, -0.25f, 0.25f, 0.0f),
                test.expected, 1.0e-6f);
  }
}

TEST_F(SamplerMatrixSpec, ComparisonAlwaysAndNeverProduceBooleanResults) {
  auto sampler = PointSampler();
  sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  EXPECT_NEAR(RunSingleSample(sampler, 0.25f, 0.25f, 0.0f, true, 0.5f),
              1.0f, 1.0e-6f);
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  EXPECT_NEAR(RunSingleSample(sampler, 0.25f, 0.25f, 0.0f, true, 0.5f),
              0.0f, 1.0e-6f);
}

TEST_F(SamplerMatrixSpec, AnisotropicSamplerExecutesAtSupportedFeatureLevel) {
  D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
  D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
  levels.NumFeatureLevels = ARRAYSIZE(requested);
  levels.pFeatureLevelsRequested = requested;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels)),
            S_OK);
  if (levels.MaxSupportedFeatureLevel < D3D_FEATURE_LEVEL_11_0)
    GTEST_SKIP() << "anisotropic filtering requires feature level 11_0";
  auto sampler = PointSampler();
  sampler.Filter = D3D12_FILTER_ANISOTROPIC;
  sampler.MaxAnisotropy = D3D12_MAX_MAXANISOTROPY;
  EXPECT_NEAR(RunSingleSample(sampler, 0.25f, 0.25f, 0.0f), 1.0f,
              1.0e-6f);
}

TEST_F(SamplerMatrixSpec, MinAndMaxLodClampExplicitSampling) {
  auto sampler = PointSampler();
  sampler.MinLOD = 2.0f;
  EXPECT_NEAR(RunMipSample(sampler, 0.0f), 4.0f, 1.0e-6f);
  sampler = PointSampler();
  sampler.MaxLOD = 0.0f;
  EXPECT_NEAR(RunMipSample(sampler, 2.0f), 1.0f, 1.0e-6f);
}

TEST_F(SamplerMatrixSpec, SamplerMipLodBiasOffsetsExplicitLod) {
  auto sampler = PointSampler();
  sampler.MipLODBias = 2.0f;
  EXPECT_NEAR(RunMipSample(sampler, 0.0f), 4.0f, 1.0e-6f);
}

TEST_F(SamplerMatrixSpec, ExplicitGradientsSelectMipLevel) {
  const auto sampler = PointSampler();
  EXPECT_NEAR(RunMipGradientSample(sampler, 1.0f / 4.0f), 1.0f,
              1.0e-6f);
  EXPECT_NEAR(RunMipGradientSample(sampler, 1.0f), 4.0f,
              1.0e-6f);
}

TEST_F(SamplerMatrixSpec, SamplerMipLodBiasOffsetsGradientSelectedMip) {
  auto sampler = PointSampler();
  sampler.MipLODBias = 1.0f;
  EXPECT_NEAR(RunMipGradientSample(sampler, 1.0f / 4.0f), 2.0f,
              1.0e-6f);
  EXPECT_NEAR(RunMipGradientSample(sampler, 1.0f / 2.0f), 4.0f,
              1.0e-6f);
}

TEST_F(SamplerMatrixSpec, StaticAndDynamicSamplerProduceSameValue) {
  D3D12_DESCRIPTOR_RANGE resources[2] = {};
  resources[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resources[0].NumDescriptors = 1;
  resources[0].OffsetInDescriptorsFromTableStart = 0;
  resources[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  resources[1].NumDescriptors = 1;
  resources[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = resources;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 4;
  D3D12_STATIC_SAMPLER_DESC static_sampler = {};
  static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  static_sampler.MaxAnisotropy = 1;
  static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  static_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
  static_sampler.ShaderRegister = 0;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &static_sampler;
  auto static_root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(static_root);
  const D3D12_SHADER_BYTECODE bytecode = {
      sample_shader_.bytecode->GetBufferPointer(),
      sample_shader_.bytecode->GetBufferSize()};
  auto static_pipeline =
      context_.CreateComputePipeline(static_root.get(), bytecode);
  ASSERT_TRUE(static_pipeline);

  constexpr std::array<float, 4> pixels = {1.0f, 2.0f, 3.0f, 4.0f};
  auto texture = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = CreateOutput(1);
  auto resource_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(output);
  ASSERT_TRUE(resource_heap);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), pixels.data(), 2 * sizeof(float),
                pixels.size() * sizeof(float)),
            S_OK);
  CreateViews(texture.get(), 1, output.get(), 1, resource_heap.get());
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *heaps[] = {resource_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(static_root.get());
  context_.list()->SetPipelineState(static_pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, resource_heap->GetGPUDescriptorHandleForHeapStart());
  const UINT constants[] = {std::bit_cast<UINT>(-0.25f),
                            std::bit_cast<UINT>(0.25f),
                            std::bit_cast<UINT>(0.0f), 0};
  context_.list()->SetComputeRoot32BitConstants(1, 4, constants, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(float), &bytes), S_OK);
  float static_value = 0.0f;
  std::memcpy(&static_value, bytes.data(), sizeof(static_value));
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  auto dynamic_sampler = PointSampler(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  const float dynamic_value =
      RunSingleSample(dynamic_sampler, -0.25f, 0.25f, 0.0f);
  EXPECT_NEAR(static_value, dynamic_value, 1.0e-6f);
  EXPECT_NEAR(static_value, 2.0f, 1.0e-6f);
}

TEST_F(SamplerMatrixSpec, SwitchingSamplerHeapRebindsDescriptorTable) {
  constexpr std::array<float, 4> pixels = {1.0f, 2.0f, 3.0f, 4.0f};
  auto texture = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = CreateOutput(2);
  auto resources = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto clamp_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  auto wrap_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(output);
  ASSERT_TRUE(resources);
  ASSERT_TRUE(clamp_heap);
  ASSERT_TRUE(wrap_heap);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), pixels.data(), 2 * sizeof(float),
                pixels.size() * sizeof(float)),
            S_OK);
  CreateViews(texture.get(), 1, output.get(), 2, resources.get());
  auto clamp = PointSampler(D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
  auto wrap = PointSampler(D3D12_TEXTURE_ADDRESS_MODE_WRAP);
  context_.device()->CreateSampler(
      &clamp, clamp_heap->GetCPUDescriptorHandleForHeapStart());
  context_.device()->CreateSampler(
      &wrap, wrap_heap->GetCPUDescriptorHandleForHeapStart());
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  context_.list()->SetComputeRootSignature(sample_root_.get());
  context_.list()->SetPipelineState(sample_pipeline_.get());
  const auto resource_gpu = resources->GetGPUDescriptorHandleForHeapStart();
  const UINT constants[] = {std::bit_cast<UINT>(-0.25f),
                            std::bit_cast<UINT>(0.25f),
                            std::bit_cast<UINT>(0.0f), 0};

  ID3D12DescriptorHeap *first_heaps[] = {resources.get(), clamp_heap.get()};
  context_.list()->SetDescriptorHeaps(2, first_heaps);
  context_.list()->SetComputeRootDescriptorTable(0, resource_gpu);
  context_.list()->SetComputeRootDescriptorTable(
      1, clamp_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstants(2, 4, constants, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), output.get());

  ID3D12DescriptorHeap *second_heaps[] = {resources.get(), wrap_heap.get()};
  context_.list()->SetDescriptorHeaps(2, second_heaps);
  context_.list()->SetComputeRootDescriptorTable(0, resource_gpu);
  context_.list()->SetComputeRootDescriptorTable(
      1, wrap_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstant(2, 1, 3);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), 2 * sizeof(float), &bytes),
            S_OK);
  std::array<float, 2> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  EXPECT_NEAR(actual[0], 1.0f, 1.0e-6f);
  EXPECT_NEAR(actual[1], 2.0f, 1.0e-6f);
}

} // namespace
