#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 CreateCommittedResource heap type × buffer size matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct HeapTypeCase {
  D3D12_HEAP_TYPE heap_type;
  UINT64 size;
  D3D12_RESOURCE_STATES initial_state;
};

std::vector<HeapTypeCase> BuildHeapTypeCases() {
  std::vector<HeapTypeCase> cases;
  const D3D12_HEAP_TYPE heaps[] = {
      D3D12_HEAP_TYPE_DEFAULT, D3D12_HEAP_TYPE_UPLOAD, D3D12_HEAP_TYPE_READBACK};
  const UINT64 sizes[] = {1, 4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144};
  for (const auto heap : heaps) {
    for (const UINT64 size : sizes) {
      D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
      if (heap == D3D12_HEAP_TYPE_UPLOAD)
        state = D3D12_RESOURCE_STATE_GENERIC_READ;
      else if (heap == D3D12_HEAP_TYPE_READBACK)
        state = D3D12_RESOURCE_STATE_COPY_DEST;
      cases.push_back({heap, size, state});
    }
  }
  // Dense DEFAULT ladder.
  for (UINT64 size = 8; size <= 2048; size += 8)
    cases.push_back(
        {D3D12_HEAP_TYPE_DEFAULT, size, D3D12_RESOURCE_STATE_COMMON});
  return cases;
}

class HeapTypeMatrixSpec : public ::testing::TestWithParam<HeapTypeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(HeapTypeMatrixSpec, CreatesBufferOnRequestedHeapType) {
  const auto &test = GetParam();
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = test.heap_type;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = test.size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, test.initial_state, nullptr,
                IID_PPV_ARGS(resource.put())),
            S_OK)
      << "heap=" << static_cast<UINT>(test.heap_type)
      << " size=" << test.size;
  ASSERT_TRUE(resource);
  D3D12_HEAP_PROPERTIES actual_heap = {};
  D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
  ASSERT_EQ(resource->GetHeapProperties(&actual_heap, &flags), S_OK);
  EXPECT_EQ(actual_heap.Type, test.heap_type);
  EXPECT_EQ(resource->GetDesc().Width, test.size);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string HeapTypeName(const ::testing::TestParamInfo<HeapTypeCase> &info) {
  return "H" + std::to_string(static_cast<UINT>(info.param.heap_type)) + "S" +
         std::to_string(info.param.size) + "N" + std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(HeapMatrix, HeapTypeMatrixSpec,
                         ::testing::ValuesIn(BuildHeapTypeCases()),
                         HeapTypeName);

} // namespace
