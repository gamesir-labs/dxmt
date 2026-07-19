#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>
#include <string>
#include <vector>

// Plan §14.7 / §17 root descriptors: CBV/SRV/UAV GPU VA offsets matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct RootOffsetCase {
  UINT64 input_base;
  UINT64 output_base;
  UINT64 cbv_base;
  UINT byte_offset;
};

std::vector<RootOffsetCase> BuildRootOffsetCases() {
  std::vector<RootOffsetCase> cases = {{0, 0, 0, 0}};
  const UINT64 bases[] = {0, 16, 64, 128, 256, 512, 1024, 2048};
  const UINT dword_offsets[] = {0, 4, 8, 12};
  for (const UINT64 base : bases) {
    if (base == 0)
      continue;
    cases.push_back({base, 0, 0, 0});
    cases.push_back({0, base, 0, 0});
    cases.push_back({base, base, 0, 0});
  }
  for (const UINT64 cbv_base : {256ull, 512ull})
    cases.push_back({0, 0, cbv_base, 0});
  for (const UINT byte_offset : dword_offsets)
    cases.push_back({64, 128, 256, byte_offset});
  return cases;
}

class RootDescriptorOffsetMatrixSpec
    : public ::testing::TestWithParam<RootOffsetCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      ByteAddressBuffer input : register(t0);
      RWByteAddressBuffer output : register(u0);
      cbuffer Parameters : register(b0) {
        uint addend;
        uint byte_offset;
      };

      [numthreads(1, 1, 1)]
      void main() {
        output.Store(byte_offset, input.Load(byte_offset) + addend);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();

    D3D12_ROOT_PARAMETER parameters[3] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 0;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = parameters;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);
    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(),
        shader_.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(RootDescriptorOffsetMatrixSpec, ComputeLoadStoreAtGpuVaOffsets) {
  const auto &test = GetParam();
  constexpr UINT64 kBufferSize = 4096;
  constexpr UINT64 kCbvSize = 1024;
  constexpr UINT kInputValue = 0x13579bdf;
  constexpr UINT kAddend = 0x10203040;
  const UINT64 input_offset = test.input_base + test.byte_offset;
  const UINT64 output_offset = test.output_base + test.byte_offset;

  std::vector<std::uint8_t> input_bytes(kBufferSize);
  std::memcpy(input_bytes.data() + input_offset, &kInputValue,
              sizeof(kInputValue));
  std::vector<std::uint8_t> cbv_bytes(kCbvSize);
  const UINT parameters[] = {kAddend, test.byte_offset};
  std::memcpy(cbv_bytes.data() + test.cbv_base, parameters, sizeof(parameters));
  std::vector<std::uint8_t> zero_bytes(kBufferSize);

  auto input = context_.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                                     D3D12_RESOURCE_FLAG_NONE,
                                     D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(kBufferSize, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
  auto input_upload = context_.CreateUploadBuffer(
      kBufferSize, input_bytes.data(), input_bytes.size());
  auto output_upload = context_.CreateUploadBuffer(
      kBufferSize, zero_bytes.data(), zero_bytes.size());
  auto constants = context_.CreateUploadBuffer(kCbvSize, cbv_bytes.data(),
                                               cbv_bytes.size());
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  ASSERT_TRUE(input_upload);
  ASSERT_TRUE(output_upload);
  ASSERT_TRUE(constants);

  context_.list()->CopyBufferRegion(input.get(), 0, input_upload.get(), 0,
                                    kBufferSize);
  context_.list()->CopyBufferRegion(output.get(), 0, output_upload.get(), 0,
                                    kBufferSize);
  D3D12TestContext::Transition(
      context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  context_.list()->SetComputeRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->SetComputeRootShaderResourceView(
      0, input->GetGPUVirtualAddress() + test.input_base);
  context_.list()->SetComputeRootUnorderedAccessView(
      1, output->GetGPUVirtualAddress() + test.output_base);
  context_.list()->SetComputeRootConstantBufferView(
      2, constants->GetGPUVirtualAddress() + test.cbv_base);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), kBufferSize, &actual), S_OK);
  for (UINT64 offset = 0; offset < kBufferSize; offset += sizeof(UINT)) {
    UINT value = 0;
    std::memcpy(&value, actual.data() + offset, sizeof(value));
    EXPECT_EQ(value, offset == output_offset ? kInputValue + kAddend : 0u)
        << "in=" << test.input_base << " out=" << test.output_base
        << " cbv=" << test.cbv_base << " boff=" << test.byte_offset
        << " dword@" << offset;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string RootOffsetName(
    const ::testing::TestParamInfo<RootOffsetCase> &info) {
  return "I" + std::to_string(info.param.input_base) + "O" +
         std::to_string(info.param.output_base) + "C" +
         std::to_string(info.param.cbv_base) + "B" +
         std::to_string(info.param.byte_offset);
}

INSTANTIATE_TEST_SUITE_P(OffsetMatrix, RootDescriptorOffsetMatrixSpec,
                         ::testing::ValuesIn(BuildRootOffsetCases()),
                         RootOffsetName);

} // namespace
