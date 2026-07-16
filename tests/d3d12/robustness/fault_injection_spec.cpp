#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdlib>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

UINT ConfiguredOccurrence(const char *name) {
  const char *value = std::getenv(name);
  if (!value || !*value)
    return 0;
  char *end = nullptr;
  const auto parsed = std::strtoul(value, &end, 0);
  return end != value && !*end && parsed <= UINT_MAX
             ? static_cast<UINT>(parsed)
             : 0;
}

class D3D12CreationFaultInjectionSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12CreationFaultInjectionSpec,
       ResourceFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_RESOURCE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "resource creation fault injection is disabled";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 4096;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(resource.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(resource);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(resource);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       DescriptorHeapFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_DESCRIPTOR_HEAP_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "descriptor heap fault injection is disabled";

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 4;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = context_.device()->CreateDescriptorHeap(
        &desc, IID_PPV_ARGS(heap.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(heap);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(heap);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       ComputePipelineFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_COMPUTE_PIPELINE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "compute pipeline fault injection is disabled";

  const auto shader =
      CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  auto root = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root);
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.CS = {shader.bytecode->GetBufferPointer(),
             shader.bytecode->GetBufferSize()};
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = context_.device()->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(pipeline.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(pipeline);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(pipeline);
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
