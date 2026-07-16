#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

std::uint32_t NextDescriptorRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

class D3D12DescriptorSequenceFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12DescriptorSequenceFuzzSpec,
       CreateCopyOverwriteBindSequenceMatchesReferenceModel) {
  constexpr UINT kDescriptorCount = 16;
  constexpr UINT kOperationCount = 2048;
  std::uint32_t seed = 0xc001d00du;
  if (const char *replay_seed =
          std::getenv("DXMT_D3D12_DESCRIPTOR_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid descriptor fuzz seed";
    ASSERT_EQ(*end, '\0') << "invalid descriptor fuzz seed";
    seed = static_cast<std::uint32_t>(parsed);
  }
  const std::uint32_t initial_seed = seed;
  SCOPED_TRACE(::testing::Message() << "seed=0x" << std::hex
                                    << initial_seed);

  const auto shader = CompileShader(R"(
    Buffer<uint> input0 : register(t0);
    Buffer<uint> input1 : register(t1);
    Buffer<uint> input2 : register(t2);
    Buffer<uint> input3 : register(t3);
    Buffer<uint> input4 : register(t4);
    Buffer<uint> input5 : register(t5);
    Buffer<uint> input6 : register(t6);
    Buffer<uint> input7 : register(t7);
    Buffer<uint> input8 : register(t8);
    Buffer<uint> input9 : register(t9);
    Buffer<uint> input10 : register(t10);
    Buffer<uint> input11 : register(t11);
    Buffer<uint> input12 : register(t12);
    Buffer<uint> input13 : register(t13);
    Buffer<uint> input14 : register(t14);
    Buffer<uint> input15 : register(t15);
    RWBuffer<uint> output : register(u0);

    [numthreads(1, 1, 1)]
    void main() {
      output[0] = input0[0];
      output[1] = input1[0];
      output[2] = input2[0];
      output[3] = input3[0];
      output[4] = input4[0];
      output[5] = input5[0];
      output[6] = input6[0];
      output[7] = input7[0];
      output[8] = input8[0];
      output[9] = input9[0];
      output[10] = input10[0];
      output[11] = input11[0];
      output[12] = input12[0];
      output[13] = input13[0];
      output[14] = input14[0];
      output[15] = input15[0];
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = kDescriptorCount;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].OffsetInDescriptorsFromTableStart = kDescriptorCount;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kDescriptorCount + 1, true);
  auto source_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kDescriptorCount, false);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(source_heap);
  std::array<ComPtr<ID3D12Resource>, kDescriptorCount> inputs;
  std::array<std::uint32_t, kDescriptorCount> values = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_UINT;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = 1;
  for (UINT index = 0; index < kDescriptorCount; ++index) {
    values[index] = 0x10203040u + index * 0x01020408u;
    inputs[index] = context_.CreateUploadBuffer(
        sizeof(values[index]), &values[index], sizeof(values[index]));
    ASSERT_TRUE(inputs[index]);
    context_.device()->CreateShaderResourceView(
        inputs[index].get(), &srv,
        context_.CpuDescriptorHandle(source_heap.get(), index));
    context_.device()->CopyDescriptorsSimple(
        1, context_.CpuDescriptorHandle(heap.get(), index),
        context_.CpuDescriptorHandle(source_heap.get(), index),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  std::array<UINT, kDescriptorCount> model = {};
  for (UINT index = 0; index < kDescriptorCount; ++index)
    model[index] = index;
  for (UINT operation = 0; operation < kOperationCount; ++operation) {
    const UINT destination = NextDescriptorRandom(seed) % kDescriptorCount;
    const UINT source = NextDescriptorRandom(seed) % kDescriptorCount;
    if (NextDescriptorRandom(seed) & 1) {
      context_.device()->CreateShaderResourceView(
          inputs[source].get(), &srv,
          context_.CpuDescriptorHandle(heap.get(), destination));
      model[destination] = source;
    } else if (destination != source) {
      context_.device()->CopyDescriptorsSimple(
          1, context_.CpuDescriptorHandle(heap.get(), destination),
          context_.CpuDescriptorHandle(source_heap.get(), source),
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      model[destination] = source;
    }
  }

  constexpr UINT64 kOutputSize =
      kDescriptorCount * sizeof(std::uint32_t);
  auto output = context_.CreateBuffer(
      kOutputSize, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_TRUE(output);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kDescriptorCount;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), kDescriptorCount));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), kOutputSize, &bytes), S_OK);
  ASSERT_EQ(bytes.size(), kOutputSize);
  std::array<std::uint32_t, kDescriptorCount> actual = {};
  std::memcpy(actual.data(), bytes.data(), bytes.size());
  for (UINT index = 0; index < kDescriptorCount; ++index)
    EXPECT_EQ(actual[index], values[model[index]]) << "descriptor " << index;
}

} // namespace
