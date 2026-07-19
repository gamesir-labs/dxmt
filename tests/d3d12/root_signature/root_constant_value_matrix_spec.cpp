#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan root signature: root 32-bit constant value × slot matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct RootConstCase {
  UINT slot;   // 0..3
  UINT value;
};

std::vector<RootConstCase> BuildRootConstCases() {
  std::vector<RootConstCase> cases;
  const UINT values[] = {
      0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256,
      511, 512, 1023, 1024, 2047, 2048, 4095, 4096, 8191, 8192, 16383, 16384,
      32767, 32768, 65535, 65536, 0x00ffffff, 0x7fffffff, 0x80000000,
      0xffffffff, 0x12345678, 0xa5a5a5a5, 0x5a5a5a5a, 0xdeadbeef, 0x0f0f0f0f,
      0xf0f0f0f0, 0x01010101, 0x10101010, 0xaaaaaaaa, 0x55555555, 42, 100,
      999, 1000, 9999, 10000, 99999, 100000, 1000000, 0xabcdef01, 0x10abcdef,
  };
  for (UINT slot = 0; slot < 4; ++slot) {
    for (const UINT value : values)
      cases.push_back({slot, value});
    // Dense small ladder per slot.
    for (UINT v = 0; v < 64; ++v)
      cases.push_back({slot, v * 17u + slot});
  }
  return cases;
}

class RootConstantValueMatrixSpec
    : public ::testing::TestWithParam<RootConstCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      RWByteAddressBuffer output : register(u0);
      cbuffer Constants : register(b0) {
        uint c0;
        uint c1;
        uint c2;
        uint c3;
      };
      [numthreads(1,1,1)]
      void main() {
        output.Store(0, c0);
        output.Store(4, c1);
        output.Store(8, c2);
        output.Store(12, c3);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 4;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    root_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_);
    const D3D12_SHADER_BYTECODE bc = {shader_.bytecode->GetBufferPointer(),
                                      shader_.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateComputePipeline(root_.get(), bc);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(RootConstantValueMatrixSpec, SlotValueRoundTripsThroughCompute) {
  const auto &test = GetParam();
  UINT constants[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
  constants[test.slot] = test.value;

  auto output = context_.CreateBuffer(
      16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 4;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->SetComputeRoot32BitConstants(1, 4, constants, 0);
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), 16, &bytes), S_OK);
  for (UINT i = 0; i < 4; ++i) {
    UINT actual = 0;
    std::memcpy(&actual, bytes.data() + i * 4, 4);
    EXPECT_EQ(actual, constants[i]) << "slot " << i << " set=" << test.slot;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string RootConstName(
    const ::testing::TestParamInfo<RootConstCase> &info) {
  return "S" + std::to_string(info.param.slot) + "V" +
         std::to_string(info.param.value) + "I" + std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(ValueMatrix, RootConstantValueMatrixSpec,
                         ::testing::ValuesIn(BuildRootConstCases()),
                         RootConstName);

} // namespace
