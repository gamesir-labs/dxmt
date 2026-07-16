#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

ComPtr<ID3D12RootSignature>
CreateResourceRootSignature(D3D12TestContext &context, bool sampler = false) {
  D3D12_DESCRIPTOR_RANGE resource_ranges[2] = {};
  resource_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  resource_ranges[0].NumDescriptors = 1;
  resource_ranges[0].BaseShaderRegister = 0;
  resource_ranges[0].OffsetInDescriptorsFromTableStart = 0;
  resource_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  resource_ranges[1].NumDescriptors = 1;
  resource_ranges[1].BaseShaderRegister = 0;
  resource_ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_DESCRIPTOR_RANGE sampler_range = {};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = 1;
  sampler_range.BaseShaderRegister = 0;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = resource_ranges;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &sampler_range;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = sampler ? 2 : 1;
  desc.pParameters = parameters;
  return context.CreateRootSignature(desc);
}

class ShaderBufferSemanticSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  void RunViewCase(const char *source,
                   const D3D12_SHADER_RESOURCE_VIEW_DESC &srv,
                   const D3D12_UNORDERED_ACCESS_VIEW_DESC &uav,
                   const std::vector<UINT> &input_data,
                   const std::vector<UINT> &expected) {
    const auto shader = CompileShader(source, "cs_5_0");
    ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    auto root_signature = CreateResourceRootSignature(context_);
    ASSERT_TRUE(root_signature);
    const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                            shader.bytecode->GetBufferSize()};
    auto pipeline =
        context_.CreateComputePipeline(root_signature.get(), bytecode);
    ASSERT_TRUE(pipeline);

    const UINT64 input_size = input_data.size() * sizeof(UINT);
    const UINT64 output_size = expected.size() * sizeof(UINT);
    std::vector<UINT> zero(expected.size());
    auto input_upload = context_.CreateUploadBuffer(
        input_size, input_data.data(), static_cast<std::size_t>(input_size));
    auto output_upload = context_.CreateUploadBuffer(
        output_size, zero.data(), static_cast<std::size_t>(output_size));
    auto input = context_.CreateBuffer(
        input_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto output = context_.CreateBuffer(
        output_size, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
    ASSERT_TRUE(input_upload);
    ASSERT_TRUE(output_upload);
    ASSERT_TRUE(input);
    ASSERT_TRUE(output);
    ASSERT_TRUE(heap);

    context_.list()->CopyBufferRegion(input.get(), 0, input_upload.get(), 0,
                                      input_size);
    context_.list()->CopyBufferRegion(output.get(), 0, output_upload.get(), 0,
                                      output_size);
    D3D12TestContext::Transition(
        context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context_.device()->CreateShaderResourceView(
        input.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
    context_.device()->CreateUnorderedAccessView(
        output.get(), nullptr, &uav,
        context_.CpuDescriptorHandle(heap.get(), 1));

    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    context_.list()->Dispatch(1, 1, 1);
    D3D12TestContext::Transition(
        context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> bytes;
    ASSERT_EQ(context_.ReadbackBuffer(output.get(), output_size, &bytes), S_OK);
    ASSERT_EQ(bytes.size(), output_size);
    std::vector<UINT> actual(expected.size());
    std::memcpy(actual.data(), bytes.data(), bytes.size());
    EXPECT_EQ(actual, expected);
  }

  D3D12TestContext context_;
};

TEST_F(ShaderBufferSemanticSpec, RawBufferExecutesVectorLoadsAndStoresAtOffsets) {
  constexpr char kShader[] = R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      output.Store4(0, input.Load4(4));
      output.Store2(16, input.Load2(20));
      output.Store(24, input.Load(0));
    }
  )";
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 16;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 7;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  RunViewCase(kShader, srv, uav,
              {0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107,
               0x108, 0x109, 0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x10f},
              {0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x100});
}

TEST_F(ShaderBufferSemanticSpec, StructuredBufferPreservesStrideAndMemberLoads) {
  constexpr char kShader[] = R"(
    StructuredBuffer<uint4> input : register(t0);
    RWBuffer<uint4> output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      uint4 record = input[2];
      output[0] = record;
      output[1] = uint4(input[1].w, input[0].x, input[3].z, input[1].x);
    }
  )";
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_UNKNOWN;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 4;
  srv.Buffer.StructureByteStride = 4 * sizeof(UINT);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  RunViewCase(kShader, srv, uav,
              {10, 11, 12, 13, 20, 21, 22, 23, 30, 31, 32, 33, 40, 41,
               42, 43},
              {30, 31, 32, 33, 23, 10, 42, 20});
}

TEST_F(ShaderBufferSemanticSpec, TypedBufferExecutesVectorSwizzleAndArithmetic) {
  constexpr char kShader[] = R"(
    Buffer<uint4> input : register(t0);
    RWBuffer<uint4> output : register(u0);
    [numthreads(1, 1, 1)]
    void main() {
      output[0] = input[2].wzyx;
      output[1] = input[0] + input[1];
    }
  )";
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 4;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32G32B32A32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  RunViewCase(kShader, srv, uav,
              {1, 2, 3, 4, 10, 20, 30, 40, 100, 200, 300, 400, 1000,
               2000, 3000, 4000},
              {400, 300, 200, 100, 11, 22, 33, 44});
}

struct AtomicSemanticCase {
  const char *name;
  const char *statement;
  UINT initial;
  UINT expected;
};

class ShaderAtomicSemanticSpec
    : public ::testing::TestWithParam<AtomicSemanticCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ShaderAtomicSemanticSpec, ProducesDeterministicUavResult) {
  const auto &test = GetParam();
  const std::string source = std::string(R"(
    RWStructuredBuffer<uint> target : register(u0);
    [numthreads(32, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint original;
  )") + test.statement + R"(
    }
  )";
  const auto shader = CompileShader(source.c_str(), "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 0, nullptr,
                                               D3D12_ROOT_SIGNATURE_FLAG_NONE};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto upload = context_.CreateUploadBuffer(sizeof(UINT), &test.initial,
                                             sizeof(test.initial));
  auto target = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  context_.list()->CopyBufferRegion(target.get(), 0, upload.get(), 0,
                                    sizeof(UINT));
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      target.get(), nullptr, &uav,
      heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(target.get(), sizeof(UINT), &bytes), S_OK);
  UINT actual = 0;
  std::memcpy(&actual, bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, test.expected);
}

std::string AtomicSemanticCaseName(
    const ::testing::TestParamInfo<AtomicSemanticCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    StructuredUavAtomics, ShaderAtomicSemanticSpec,
    ::testing::Values(
        AtomicSemanticCase{"Add", "InterlockedAdd(target[0], id.x + 1, original);",
                           0, 528},
        AtomicSemanticCase{
            "And", "InterlockedAnd(target[0], ~(1u << (id.x & 15u)), original);",
            0xffffffffu, 0xffff0000u},
        AtomicSemanticCase{
            "Or", "InterlockedOr(target[0], 1u << (id.x & 15u), original);", 0,
            0x0000ffffu},
        AtomicSemanticCase{
            "Xor", "InterlockedXor(target[0], 1u << (id.x & 15u), original);", 0,
            0},
        AtomicSemanticCase{"Min", "InterlockedMin(target[0], id.x * 7, original);",
                           0xffffffffu, 0},
        AtomicSemanticCase{"Max", "InterlockedMax(target[0], id.x * 7, original);",
                           0, 217},
        AtomicSemanticCase{
            "Exchange",
            "if (id.x == 0) InterlockedExchange(target[0], 0x12345678, original);",
            7, 0x12345678},
        AtomicSemanticCase{
            "CompareExchange",
            "InterlockedCompareExchange(target[0], 10, 20, original);", 10,
            20}),
    AtomicSemanticCaseName);

enum class TextureShape { Texture2D, Texture2DArray, Texture3D };

struct TextureLoadCase {
  const char *name;
  TextureShape shape;
  const char *source;
  std::array<UINT, 5> expected;
};

class ShaderTextureLoadSemanticSpec
    : public ::testing::TestWithParam<TextureLoadCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12Resource> CreateTexture(TextureShape shape) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = shape == TextureShape::Texture3D
                         ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                         : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = shape == TextureShape::Texture2D ? 4 : 2;
    desc.Height = shape == TextureShape::Texture2D ? 3 : 2;
    desc.DepthOrArraySize = shape == TextureShape::Texture2D ? 1 : 2;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_UINT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ComPtr<ID3D12Resource> texture;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, __uuidof(ID3D12Resource),
        reinterpret_cast<void **>(texture.put()));
    return SUCCEEDED(hr) ? std::move(texture) : ComPtr<ID3D12Resource>{};
  }

  void Upload(TextureShape shape, ID3D12Resource *texture) {
    if (shape == TextureShape::Texture2D) {
      const std::array<UINT, 12> data = {100, 101, 102, 103, 110, 111,
                                         112, 113, 120, 121, 122, 123};
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture, data.data(), 4 * sizeof(UINT),
                    12 * sizeof(UINT)),
                S_OK);
      return;
    }
    if (shape == TextureShape::Texture2DArray) {
      const std::array<UINT, 4> first = {10, 11, 12, 13};
      const std::array<UINT, 4> second = {20, 21, 22, 23};
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture, first.data(), 2 * sizeof(UINT),
                    4 * sizeof(UINT), 0),
                S_OK);
      ASSERT_EQ(context_.UploadTextureAndReset(
                    texture, second.data(), 2 * sizeof(UINT),
                    4 * sizeof(UINT), 1),
                S_OK);
      return;
    }
    const std::array<UINT, 8> data = {10, 11, 12, 13, 20, 21, 22, 23};
    ASSERT_EQ(context_.UploadTextureAndReset(
                  texture, data.data(), 2 * sizeof(UINT), 4 * sizeof(UINT)),
              S_OK);
  }

  D3D12TestContext context_;
};

TEST_P(ShaderTextureLoadSemanticSpec, LoadsTexelAndReportsDimensions) {
  const auto &test = GetParam();
  const auto shader = CompileShader(test.source, "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto root_signature = CreateResourceRootSignature(context_);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);
  auto texture = CreateTexture(test.shape);
  auto output = context_.CreateBuffer(
      sizeof(test.expected), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);
  Upload(test.shape, texture.get());

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (test.shape == TextureShape::Texture2D) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
  } else if (test.shape == TextureShape::Texture2DArray) {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.ArraySize = 2;
  } else {
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Texture3D.MipLevels = 1;
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = test.expected.size();
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateShaderResourceView(
      texture.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 1));
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(test.expected), &bytes),
            S_OK);
  std::array<UINT, 5> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, test.expected);
}

std::string TextureLoadCaseName(
    const ::testing::TestParamInfo<TextureLoadCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    TextureKinds, ShaderTextureLoadSemanticSpec,
    ::testing::Values(
        TextureLoadCase{
            "Texture2D", TextureShape::Texture2D,
            R"(
              Texture2D<uint> input : register(t0);
              RWByteAddressBuffer output : register(u0);
              [numthreads(1, 1, 1)] void main() {
                uint w, h, levels;
                input.GetDimensions(0, w, h, levels);
                output.Store4(0, uint4(input.Load(int3(2, 1, 0)), w, h,
                                      levels));
                output.Store(16, 0);
              })",
            {112, 4, 3, 1, 0}},
        TextureLoadCase{
            "Texture2DArray", TextureShape::Texture2DArray,
            R"(
              Texture2DArray<uint> input : register(t0);
              RWByteAddressBuffer output : register(u0);
              [numthreads(1, 1, 1)] void main() {
                uint w, h, layers, levels;
                input.GetDimensions(0, w, h, layers, levels);
                output.Store4(0, uint4(input.Load(int4(1, 0, 1, 0)), w, h,
                                      layers));
                output.Store(16, levels);
              })",
            {21, 2, 2, 2, 1}},
        TextureLoadCase{
            "Texture3D", TextureShape::Texture3D,
            R"(
              Texture3D<uint> input : register(t0);
              RWByteAddressBuffer output : register(u0);
              [numthreads(1, 1, 1)] void main() {
                uint w, h, depth, levels;
                input.GetDimensions(0, w, h, depth, levels);
                output.Store4(0, uint4(input.Load(int4(0, 1, 1, 0)), w, h,
                                      depth));
                output.Store(16, levels);
              })",
            {22, 2, 2, 2, 1}}),
    TextureLoadCaseName);

class ShaderTextureSamplingSemanticSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(ShaderTextureSamplingSemanticSpec, GatherAndSampleLevelAgreeWithTexels) {
  const auto shader = CompileShader(R"(
    Texture2D<float> input : register(t0);
    SamplerState input_sampler : register(s0);
    RWByteAddressBuffer output : register(u0);
    [numthreads(1, 1, 1)] void main() {
      float4 gathered = input.GatherRed(input_sampler, float2(0.5, 0.5));
      float center = input.SampleLevel(input_sampler, float2(0.5, 0.5), 0);
      output.Store2(0, uint2(asuint(dot(gathered, 1.0)), asuint(center)));
    }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto root_signature = CreateResourceRootSignature(context_, true);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  const std::array<float, 4> pixels = {1.0f, 2.0f, 3.0f, 4.0f};
  auto texture = context_.CreateTexture2D(
      2, 2, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(
      2 * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto resource_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  auto sampler_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(output);
  ASSERT_TRUE(resource_heap);
  ASSERT_TRUE(sampler_heap);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), pixels.data(), 2 * sizeof(float),
                pixels.size() * sizeof(float)),
            S_OK);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_FLOAT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 2;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateShaderResourceView(
      texture.get(), &srv,
      context_.CpuDescriptorHandle(resource_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(resource_heap.get(), 1));
  D3D12_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  context_.device()->CreateSampler(
      &sampler, sampler_heap->GetCPUDescriptorHandleForHeapStart());
  D3D12TestContext::Transition(
      context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  ID3D12DescriptorHeap *heaps[] = {resource_heap.get(), sampler_heap.get()};
  context_.list()->SetDescriptorHeaps(2, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, resource_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRootDescriptorTable(
      1, sampler_heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), 2 * sizeof(UINT), &bytes),
            S_OK);
  std::array<float, 2> actual = {};
  std::memcpy(actual.data(), bytes.data(), sizeof(actual));
  EXPECT_NEAR(actual[0], 10.0f, 1e-6f);
  EXPECT_NEAR(actual[1], 4.0f, 1e-6f);
}

struct DerivativeSemanticCase {
  const char *name;
  const char *expression;
  float expected;
};

class ShaderDerivativeSemanticSpec
    : public ::testing::TestWithParam<DerivativeSemanticCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ShaderDerivativeSemanticSpec, ProducesStableFramebufferGradient) {
  const auto &test = GetParam();
  const std::string source = std::string(R"(
    float main(float4 position : SV_Position) : SV_Target {
      return )") + test.expression + ";\n}\n";
  const auto pixel = CompileShader(source.c_str(), "ps_5_0");
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {pixel.bytecode->GetBufferPointer(),
                                          pixel.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateGraphicsPipeline(
      root_signature.get(), DXGI_FORMAT_R32_FLOAT, bytecode);
  ASSERT_TRUE(pipeline);

  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 8;
  auto target = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R32_FLOAT,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                            false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);
  constexpr FLOAT clear[4] = {};
  context_.list()->ClearRenderTargetView(rtv, clear, 0, nullptr);
  context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  context_.list()->SetGraphicsRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  const D3D12_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(kWidth),
                                   static_cast<FLOAT>(kHeight), 0, 1};
  const D3D12_RECT scissor = {0, 0, kWidth, kHeight};
  context_.list()->RSSetViewports(1, &viewport);
  context_.list()->RSSetScissorRects(1, &scissor);
  context_.list()->DrawInstanced(3, 1, 0, 0);
  D3D12TestContext::Transition(
      context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  for (UINT y = 1; y + 1 < kHeight; ++y) {
    for (UINT x = 1; x + 1 < kWidth; ++x) {
      float actual = 0.0f;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_NEAR(actual, test.expected, 1e-5f)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

std::string DerivativeSemanticCaseName(
    const ::testing::TestParamInfo<DerivativeSemanticCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    PixelQuads, ShaderDerivativeSemanticSpec,
    ::testing::Values(
        DerivativeSemanticCase{"Ddx", "ddx(position.x)", 1.0f},
        DerivativeSemanticCase{"Ddy", "ddy(position.y)", 1.0f},
        DerivativeSemanticCase{"DdxCoarse", "ddx_coarse(position.x)", 1.0f},
        DerivativeSemanticCase{"DdyCoarse", "ddy_coarse(position.y)", 1.0f},
        DerivativeSemanticCase{"DdxFine", "ddx_fine(position.x)", 1.0f},
        DerivativeSemanticCase{"DdyFine", "ddy_fine(position.y)", 1.0f},
        DerivativeSemanticCase{
            "Fwidth", "fwidth(position.x + 2.0 * position.y)", 3.0f}),
    DerivativeSemanticCaseName);

} // namespace
