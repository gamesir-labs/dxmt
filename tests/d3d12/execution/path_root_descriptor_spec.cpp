#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::D3D12TestContext;

class ExecutionPathRootDescriptorSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

DXMT_SERIAL_TEST_DOMAIN("ExecutionPathRootDescriptorSpec.*",
                        "root-descriptor");

DXMT_SERIAL_TEST_F(
    ExecutionPathRootDescriptorSpec,
    RootDescriptorsAndConstantsSurviveCopyFallbackExactlyOnce) {
  constexpr std::array<std::uint32_t, 8> input_values = {
      1u, 3u, 7u, 15u, 31u, 63u, 127u, 255u};
  constexpr std::array<std::uint32_t, 2> constants = {3u, 5u};
  constexpr std::array<std::uint32_t, 6> copy_sentinel = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u,
      0xd0e0f000u, 0x55aa55aau, 0xaa55aa55u};
  constexpr UINT output_dword_count = input_values.size();
  constexpr UINT64 output_size = output_dword_count * sizeof(std::uint32_t);
  constexpr UINT64 counter_size = sizeof(std::uint32_t);
  constexpr UINT64 sentinel_size = copy_sentinel.size() * sizeof(std::uint32_t);

  const auto shader = CompileShader(R"(
    cbuffer Constants : register(b0) {
      uint multiplier;
      uint bias;
    };
    ByteAddressBuffer input_buffer : register(t0);
    RWByteAddressBuffer output_buffer : register(u0);
    RWStructuredBuffer<uint> dispatch_counter : register(u1);

    [numthreads(8, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID) {
      uint previous;
      if (id.x == 0)
        InterlockedAdd(dispatch_counter[0], 1u, previous);
      uint value = input_buffer.Load(id.x * 4u);
      output_buffer.Store(id.x * 4u, value * multiplier + bias);
    }
  )",
                                    "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  std::array<D3D12_ROOT_PARAMETER, 4> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.Num32BitValues = constants.size();
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].Descriptor.ShaderRegister = 0;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[2].Descriptor.ShaderRegister = 0;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[3].Descriptor.ShaderRegister = 1;
  parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = parameters.size();
  root_desc.pParameters = parameters.data();
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);

  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  const std::array<std::uint32_t, output_dword_count> zero_output = {};
  auto input_upload = context_.CreateUploadBuffer(
      sizeof(input_values), input_values.data(), sizeof(input_values));
  auto zero_upload = context_.CreateUploadBuffer(
      sizeof(zero_output), zero_output.data(), sizeof(zero_output));
  constexpr std::uint32_t zero_counter = 0;
  auto counter_upload = context_.CreateUploadBuffer(
      counter_size, &zero_counter, sizeof(zero_counter));
  auto sentinel_upload = context_.CreateUploadBuffer(
      sizeof(copy_sentinel), copy_sentinel.data(), sizeof(copy_sentinel));
  auto input = context_.CreateBuffer(
      sizeof(input_values), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(
      output_size, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto counter = context_.CreateBuffer(
      counter_size, D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto sentinel_copy = context_.CreateBuffer(
      sentinel_size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto readback = context_.CreateBuffer(
      output_size + counter_size + sentinel_size, D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(input_upload);
  ASSERT_TRUE(zero_upload);
  ASSERT_TRUE(counter_upload);
  ASSERT_TRUE(sentinel_upload);
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  ASSERT_TRUE(counter);
  ASSERT_TRUE(sentinel_copy);
  ASSERT_TRUE(readback);

  context_.list()->CopyBufferRegion(input.get(), 0, input_upload.get(), 0,
                                    sizeof(input_values));
  context_.list()->CopyBufferRegion(output.get(), 0, zero_upload.get(), 0,
                                    output_size);
  context_.list()->CopyBufferRegion(counter.get(), 0, counter_upload.get(), 0,
                                    counter_size);
  D3D12TestContext::Transition(
      context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // Bind all root state once. The CopyBufferRegion below is a fallback record;
  // neither the root descriptors nor constants are rebound around it.
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRoot32BitConstants(
      0, constants.size(), constants.data(), 0);
  context_.list()->SetComputeRootShaderResourceView(
      1, input->GetGPUVirtualAddress());
  context_.list()->SetComputeRootUnorderedAccessView(
      2, output->GetGPUVirtualAddress());
  context_.list()->SetComputeRootUnorderedAccessView(
      3, counter->GetGPUVirtualAddress());

  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::UavBarrier(context_.list(), nullptr);

  context_.list()->CopyBufferRegion(sentinel_copy.get(), 0,
                                    sentinel_upload.get(), 0, sentinel_size);

  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), counter.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12TestContext::Transition(
      context_.list(), sentinel_copy.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyBufferRegion(readback.get(), 0, output.get(), 0,
                                    output_size);
  context_.list()->CopyBufferRegion(readback.get(), output_size, counter.get(),
                                    0, counter_size);
  context_.list()->CopyBufferRegion(readback.get(), output_size + counter_size,
                                    sentinel_copy.get(), 0, sentinel_size);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {
      0, static_cast<SIZE_T>(output_size + counter_size + sentinel_size)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  const auto *actual = static_cast<const std::uint32_t *>(mapping);
  for (std::size_t index = 0; index < input_values.size(); ++index) {
    const std::uint32_t expected =
        input_values[index] * constants[0] + constants[1];
    EXPECT_EQ(actual[index], expected) << "element " << index;
  }
  EXPECT_EQ(actual[output_dword_count], 2u)
      << "each native dispatch must execute exactly once";
  EXPECT_EQ(std::memcmp(static_cast<const std::uint8_t *>(mapping) +
                            output_size + counter_size,
                        copy_sentinel.data(), sentinel_size),
            0)
      << "fallback copy must not be dropped or corrupted";
  const D3D12_RANGE no_write = {0, 0};
  readback->Unmap(0, &no_write);
}

} // namespace
