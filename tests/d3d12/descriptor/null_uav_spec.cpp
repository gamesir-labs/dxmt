#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

// Public D3D12 API only: device/list/queue/descriptor creation via the test
// fixture and ID3D12* COM entry points. No DXMT production headers or symbols.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class D3D12NullUavSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12NullUavSpec, NullTypedUavReadReturnsZeroAndWriteIsDiscarded) {
  const auto shader = CompileShader(R"(
    RWBuffer<uint> target : register(u0);
    RWBuffer<uint> probe : register(u1);
    [numthreads(1, 1, 1)]
    void main() {
      uint loaded = target[0];
      probe[0] = loaded;
      target[0] = 0xdeadbeefu;
      probe[1] = target[0];
    }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 2;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root_signature = context_.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  constexpr UINT kSentinel = 0x13579bdfu;
  constexpr std::array<UINT, 2> kProbeInitial = {kSentinel, kSentinel};
  auto probe = context_.CreateBuffer(
      sizeof(kProbeInitial), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(probe);
  ASSERT_TRUE(heap);

  {
    auto upload = context_.CreateUploadBuffer(sizeof(kProbeInitial),
                                              kProbeInitial.data(),
                                              sizeof(kProbeInitial));
    ASSERT_TRUE(upload);
    D3D12TestContext::Transition(
        context_.list(), probe.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    context_.list()->CopyBufferRegion(probe.get(), 0, upload.get(), 0,
                                      sizeof(kProbeInitial));
    D3D12TestContext::Transition(
        context_.list(), probe.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
    ASSERT_EQ(context_.ResetCommandList(), S_OK);
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav = {};
  null_uav.Format = DXGI_FORMAT_R32_UINT;
  null_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  null_uav.Buffer.NumElements = 1;
  context_.device()->CreateUnorderedAccessView(
      nullptr, nullptr, &null_uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC probe_uav = {};
  probe_uav.Format = DXGI_FORMAT_R32_UINT;
  probe_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  probe_uav.Buffer.NumElements = 2;
  context_.device()->CreateUnorderedAccessView(
      probe.get(), nullptr, &probe_uav,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), probe.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(probe.get(), sizeof(kProbeInitial), &bytes),
            S_OK);
  UINT loaded = 0;
  UINT after_store = 0;
  std::memcpy(&loaded, bytes.data(), sizeof(loaded));
  std::memcpy(&after_store, bytes.data() + sizeof(UINT), sizeof(after_store));
  EXPECT_EQ(loaded, 0u);
  EXPECT_EQ(after_store, 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12NullUavSpec, NullTexture2dUavWriteIsDiscardedWhereDefined) {
  const auto shader = CompileShader(R"(
    RWTexture2D<uint> target : register(u0);
    RWBuffer<uint> probe : register(u1);
    [numthreads(1, 1, 1)]
    void main() {
      uint loaded = target[uint2(0, 0)];
      probe[0] = loaded;
      target[uint2(0, 0)] = 0xdeadbeefu;
      probe[1] = target[uint2(0, 0)];
    }
  )", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = 2;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  auto root_signature = context_.CreateRootSignature(root_desc);
  const D3D12_SHADER_BYTECODE bytecode = {
      shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
  auto pipeline = context_.CreateComputePipeline(root_signature.get(), bytecode);
  constexpr std::array<UINT, 2> kProbeInitial = {0x11111111u, 0x22222222u};
  auto probe = context_.CreateBuffer(
      sizeof(kProbeInitial), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(root_signature);
  ASSERT_TRUE(pipeline);
  ASSERT_TRUE(probe);
  ASSERT_TRUE(heap);
  auto upload = context_.CreateUploadBuffer(
      sizeof(kProbeInitial), kProbeInitial.data(), sizeof(kProbeInitial));
  ASSERT_TRUE(upload);
  context_.list()->CopyBufferRegion(probe.get(), 0, upload.get(), 0,
                                    sizeof(kProbeInitial));
  D3D12TestContext::Transition(
      context_.list(), probe.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);

  D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav = {};
  null_uav.Format = DXGI_FORMAT_R32_UINT;
  null_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  context_.device()->CreateUnorderedAccessView(
      nullptr, nullptr, &null_uav,
      context_.CpuDescriptorHandle(heap.get(), 0));
  D3D12_UNORDERED_ACCESS_VIEW_DESC probe_uav = {};
  probe_uav.Format = DXGI_FORMAT_R32_UINT;
  probe_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  probe_uav.Buffer.NumElements = 2;
  context_.device()->CreateUnorderedAccessView(
      probe.get(), nullptr, &probe_uav,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), probe.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(probe.get(), sizeof(kProbeInitial), &bytes),
            S_OK);
  UINT loaded = 0;
  UINT after_store = 0;
  std::memcpy(&loaded, bytes.data(), sizeof(loaded));
  std::memcpy(&after_store, bytes.data() + sizeof(UINT), sizeof(after_store));
  EXPECT_EQ(loaded, 0u);
  EXPECT_EQ(after_store, 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
