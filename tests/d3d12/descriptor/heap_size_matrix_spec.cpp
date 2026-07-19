#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 descriptor heap creation matrix over type/count/visibility.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct HeapSizeCase {
  D3D12_DESCRIPTOR_HEAP_TYPE type;
  UINT count;
  D3D12_DESCRIPTOR_HEAP_FLAGS flags;
  bool expect_success;
};

std::vector<HeapSizeCase> BuildHeapSizeCases() {
  std::vector<HeapSizeCase> cases;
  const D3D12_DESCRIPTOR_HEAP_TYPE types[] = {
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
  };
  const UINT counts[] = {1, 2, 7, 8, 31, 32, 33, 63, 64, 256, 1024, 4096};
  for (const auto type : types) {
    for (const UINT count : counts) {
      cases.push_back({type, count, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, true});
      const bool shader_visible =
          type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
          type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
      if (shader_visible) {
        cases.push_back({type, count,
                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, true});
      } else {
        // RTV/DSV cannot be shader visible.
        cases.push_back({type, count,
                         D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, false});
      }
    }
  }
  // Invalid zero count.
  for (const auto type : types)
    cases.push_back({type, 0, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, false});
  return cases;
}

class DescriptorHeapSizeMatrixSpec
    : public ::testing::TestWithParam<HeapSizeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(DescriptorHeapSizeMatrixSpec, CreateMatchesExpectationAndDesc) {
  const auto &test = GetParam();
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = test.type;
  desc.NumDescriptors = test.count;
  desc.Flags = test.flags;
  ComPtr<ID3D12DescriptorHeap> heap;
  const HRESULT hr = context_.device()->CreateDescriptorHeap(
      &desc, IID_PPV_ARGS(heap.put()));
  if (!test.expect_success) {
    EXPECT_HRESULT_FAILED(hr);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return;
  }
  ASSERT_EQ(hr, S_OK) << "type=" << static_cast<UINT>(test.type)
                      << " count=" << test.count
                      << " flags=" << static_cast<UINT>(test.flags);
  ASSERT_TRUE(heap);
  const auto actual = heap->GetDesc();
  EXPECT_EQ(actual.Type, test.type);
  EXPECT_EQ(actual.NumDescriptors, test.count);
  EXPECT_EQ(actual.Flags, test.flags);
  EXPECT_NE(heap->GetCPUDescriptorHandleForHeapStart().ptr, 0u);
  if (test.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
    EXPECT_NE(heap->GetGPUDescriptorHandleForHeapStart().ptr, 0u);
  } else {
    EXPECT_EQ(heap->GetGPUDescriptorHandleForHeapStart().ptr, 0u);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string HeapSizeName(const ::testing::TestParamInfo<HeapSizeCase> &info) {
  const char *type = "Other";
  switch (info.param.type) {
  case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
    type = "CbvSrvUav";
    break;
  case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
    type = "Sampler";
    break;
  case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
    type = "Rtv";
    break;
  case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
    type = "Dsv";
    break;
  }
  return std::string(type) + "_N" + std::to_string(info.param.count) +
         (info.param.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE ? "_SV"
                                                                      : "_CPU") +
         (info.param.expect_success ? "_Ok" : "_Fail");
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, DescriptorHeapSizeMatrixSpec,
                         ::testing::ValuesIn(BuildHeapSizeCases()),
                         HeapSizeName);

} // namespace
