#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"
#include "shaders/group_shared.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::GroupSharedComputeShader;

struct OutputResources {
  ComPtr<ID3D12Resource> buffer;
  ComPtr<ID3D12DescriptorHeap> heap;
};

class ComputeDispatchSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    dispatch_shader_ = CompileShader(R"(
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        uint width;
        uint height;
        uint base;
      };

      [numthreads(2, 2, 1)]
      void main(uint3 id : SV_DispatchThreadID) {
        const uint index = base + id.x + width * (id.y + height * id.z);
        const uint value = 0x80000000u | id.x | (id.y << 8) | (id.z << 16);
        output.Store(index * 4, value);
      }
    )",
                                     "cs_5_0");
    ASSERT_EQ(dispatch_shader_.result, S_OK)
        << dispatch_shader_.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 3;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);
    dispatch_pipeline_ = CreatePipeline(dispatch_shader_);
    ASSERT_TRUE(dispatch_pipeline_);
  }

  ComPtr<ID3D12PipelineState>
  CreatePipeline(const dxmt::test::ShaderCompilation &shader) {
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    return context_.CreateComputePipeline(root_signature_.get(), bytecode);
  }

  OutputResources CreateOutput(UINT word_count) {
    return CreateOutput(std::vector<UINT>(word_count));
  }

  OutputResources CreateOutput(const std::vector<UINT> &initial) {
    const UINT word_count = static_cast<UINT>(initial.size());
    auto upload = context_.CreateUploadBuffer(
        word_count * sizeof(UINT), initial.data(),
        initial.size() * sizeof(UINT));
    OutputResources resources;
    resources.buffer = context_.CreateBuffer(
        word_count * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    resources.heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    EXPECT_TRUE(upload);
    EXPECT_TRUE(resources.buffer);
    EXPECT_TRUE(resources.heap);
    if (!upload || !resources.buffer || !resources.heap)
      return resources;

    context_.list()->CopyBufferRegion(
        resources.buffer.get(), 0, upload.get(), 0,
        word_count * sizeof(UINT));
    D3D12TestContext::Transition(
        context_.list(), resources.buffer.get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R32_TYPELESS;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = word_count;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    context_.device()->CreateUnorderedAccessView(
        resources.buffer.get(), nullptr, &uav,
        resources.heap->GetCPUDescriptorHandleForHeapStart());
    return resources;
  }

  void Bind(const OutputResources &output, ID3D12PipelineState *pipeline) {
    ID3D12DescriptorHeap *heaps[] = {output.heap.get()};
    context_.list()->SetDescriptorHeaps(1, heaps);
    context_.list()->SetComputeRootSignature(root_signature_.get());
    context_.list()->SetPipelineState(pipeline);
    context_.list()->SetComputeRootDescriptorTable(
        0, output.heap->GetGPUDescriptorHandleForHeapStart());
  }

  std::vector<UINT> Readback(OutputResources &output, UINT word_count) {
    D3D12TestContext::Transition(
        context_.list(), output.buffer.get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::vector<std::uint8_t> bytes;
    EXPECT_EQ(context_.ReadbackBuffer(output.buffer.get(),
                                      word_count * sizeof(UINT), &bytes),
              S_OK);
    std::vector<UINT> values(word_count);
    if (bytes.size() == values.size() * sizeof(UINT))
      std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation dispatch_shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> dispatch_pipeline_;
};

TEST_F(ComputeDispatchSpec, DispatchDimensionMatrix) {
  constexpr UINT kWordCount = 64;
  auto output = CreateOutput(kWordCount);
  ASSERT_TRUE(output.buffer);
  ASSERT_TRUE(output.heap);
  Bind(output, dispatch_pipeline_.get());

  const UINT small[] = {2, 2, 0};
  context_.list()->SetComputeRoot32BitConstants(1, 3, small, 0);
  context_.list()->Dispatch(1, 1, 1);
  const UINT zero_x[] = {2, 2, 4};
  context_.list()->SetComputeRoot32BitConstants(1, 3, zero_x, 0);
  context_.list()->Dispatch(0, 1, 1);
  const UINT zero_y[] = {2, 2, 8};
  context_.list()->SetComputeRoot32BitConstants(1, 3, zero_y, 0);
  context_.list()->Dispatch(1, 0, 1);
  const UINT zero_z[] = {2, 2, 12};
  context_.list()->SetComputeRoot32BitConstants(1, 3, zero_z, 0);
  context_.list()->Dispatch(1, 1, 0);
  const UINT large[] = {4, 4, 16};
  context_.list()->SetComputeRoot32BitConstants(1, 3, large, 0);
  context_.list()->Dispatch(2, 2, 2);

  const auto actual = Readback(output, kWordCount);
  std::vector<UINT> expected(kWordCount);
  for (UINT z = 0; z < 1; ++z)
    for (UINT y = 0; y < 2; ++y)
      for (UINT x = 0; x < 2; ++x)
        expected[x + 2 * (y + 2 * z)] =
            0x80000000u | x | (y << 8) | (z << 16);
  for (UINT z = 0; z < 2; ++z)
    for (UINT y = 0; y < 4; ++y)
      for (UINT x = 0; x < 4; ++x)
        expected[16 + x + 4 * (y + 4 * z)] =
            0x80000000u | x | (y << 8) | (z << 16);
  EXPECT_EQ(actual, expected);
}

TEST_F(ComputeDispatchSpec, GroupSharedMemorySynchronizesAcrossThreads) {
  constexpr UINT kGroupCount = 3;
  constexpr UINT kGroupSize = 32;
  constexpr UINT kResultBase = kGroupCount * kGroupSize;
  std::vector<UINT> initial(kResultBase + kGroupCount);
  for (UINT group = 0; group < kGroupCount; ++group)
    for (UINT lane = 0; lane < kGroupSize; ++lane)
      initial[group * kGroupSize + lane] =
          3 + group * 1000 + lane * lane;

  auto output = CreateOutput(initial);
  ASSERT_TRUE(output.buffer);
  ASSERT_TRUE(output.heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = static_cast<UINT>(initial.size());
  uav.Buffer.StructureByteStride = sizeof(UINT);
  context_.device()->CreateUnorderedAccessView(
      output.buffer.get(), nullptr, &uav,
      output.heap->GetCPUDescriptorHandleForHeapStart());
  auto pipeline = context_.CreateComputePipeline(root_signature_.get(),
                                                 GroupSharedComputeShader());
  ASSERT_TRUE(pipeline);
  Bind(output, pipeline.get());
  const UINT parameters[] = {kGroupSize, kResultBase};
  context_.list()->SetComputeRoot32BitConstants(1, 2, parameters, 0);
  context_.list()->Dispatch(kGroupCount, 1, 1);

  auto expected = initial;
  for (UINT group = 0; group < kGroupCount; ++group)
    for (UINT lane = 0; lane < kGroupSize; ++lane)
      expected[kResultBase + group] +=
          initial[group * kGroupSize + lane];
  const auto actual = Readback(output, static_cast<UINT>(expected.size()));
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t word = 0; word < expected.size(); ++word)
    EXPECT_EQ(actual[word], expected[word]) << "word " << word;
}

} // namespace
