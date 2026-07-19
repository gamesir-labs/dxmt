#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Plan §14.7: buffer GetGPUVirtualAddress stability, uniqueness, placement.
// Public D3D12 API only.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct GpuVaSizeCase {
  UINT64 size;
};

std::vector<GpuVaSizeCase> BuildGpuVaSizeCases() {
  std::vector<GpuVaSizeCase> cases;
  for (UINT64 size : {1ull, 4ull, 16ull, 64ull, 256ull, 1024ull, 4096ull,
                      65536ull, 256 * 1024ull, 1024 * 1024ull})
    cases.push_back({size});
  // Alignment-sensitive ladder.
  for (UINT64 size = 256; size <= 4096; size += 256)
    cases.push_back({size});
  std::sort(cases.begin(), cases.end(),
            [](const GpuVaSizeCase &a, const GpuVaSizeCase &b) {
              return a.size < b.size;
            });
  cases.erase(std::unique(cases.begin(), cases.end(),
                          [](const GpuVaSizeCase &a, const GpuVaSizeCase &b) {
                            return a.size == b.size;
                          }),
              cases.end());
  return cases;
}

class GpuVaMatrixSpec : public ::testing::TestWithParam<GpuVaSizeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(GpuVaMatrixSpec, DefaultBufferGpuVaIsNonZeroAndStable) {
  const auto size = GetParam().size;
  auto buffer = context_.CreateBuffer(size, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(buffer);
  const D3D12_GPU_VIRTUAL_ADDRESS first = buffer->GetGPUVirtualAddress();
  EXPECT_NE(first, 0u) << "size=" << size;
  EXPECT_EQ(buffer->GetGPUVirtualAddress(), first);
  EXPECT_EQ(buffer->GetGPUVirtualAddress(), first);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(GpuVaMatrixSpec, UploadAndReadbackAlsoExposeStableGpuVa) {
  const auto size = GetParam().size;
  if (size > 65536)
    GTEST_SKIP() << "upload/readback GPU VA matrix capped at 64KiB";
  auto upload = context_.CreateBuffer(size, D3D12_HEAP_TYPE_UPLOAD,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
  auto readback = context_.CreateBuffer(size, D3D12_HEAP_TYPE_READBACK,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(readback);
  const auto ua = upload->GetGPUVirtualAddress();
  const auto ra = readback->GetGPUVirtualAddress();
  EXPECT_NE(ua, 0u);
  EXPECT_NE(ra, 0u);
  EXPECT_EQ(upload->GetGPUVirtualAddress(), ua);
  EXPECT_EQ(readback->GetGPUVirtualAddress(), ra);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(GpuVaMatrixSpec, IndependentBuffersHaveDistinctAddresses) {
  constexpr int kCount = 16;
  std::vector<ComPtr<ID3D12Resource>> resources;
  std::set<D3D12_GPU_VIRTUAL_ADDRESS> addresses;
  for (int i = 0; i < kCount; ++i) {
    auto buffer = context_.CreateBuffer(
        256 + static_cast<UINT64>(i) * 64, D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    ASSERT_TRUE(buffer);
    const auto address = buffer->GetGPUVirtualAddress();
    EXPECT_NE(address, 0u);
    EXPECT_TRUE(addresses.insert(address).second) << "duplicate VA " << address;
    resources.push_back(std::move(buffer));
  }
  EXPECT_EQ(addresses.size(), static_cast<size_t>(kCount));
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(GpuVaMatrixSpec, PlacedBufferGpuVaIncludesHeapPlacementOffset) {
  constexpr UINT64 kBufferSize = 4096;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = kBufferSize;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const D3D12_RESOURCE_ALLOCATION_INFO info =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(info.SizeInBytes, UINT64_MAX);
  ASSERT_GT(info.Alignment, 0u);
  // Two non-overlapping placements at 0 and one alignment step.
  const UINT64 offsets[] = {0, info.Alignment};
  const UINT64 heap_size = info.Alignment + info.SizeInBytes;

  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = heap_size;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Alignment = info.Alignment;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(context_.device()->CreateHeap(&heap_desc, IID_PPV_ARGS(heap.put())),
            S_OK);

  std::vector<D3D12_GPU_VIRTUAL_ADDRESS> vas;
  for (const UINT64 offset : offsets) {
    ComPtr<ID3D12Resource> resource;
    ASSERT_EQ(context_.device()->CreatePlacedResource(
                  heap.get(), offset, &desc, D3D12_RESOURCE_STATE_COMMON,
                  nullptr, IID_PPV_ARGS(resource.put())),
              S_OK)
        << "offset=" << offset << " align=" << info.Alignment;
    ASSERT_TRUE(resource);
    const auto va = resource->GetGPUVirtualAddress();
    EXPECT_NE(va, 0u);
    vas.push_back(va);
  }
  ASSERT_EQ(vas.size(), 2u);
  EXPECT_NE(vas[0], vas[1]);
  // Relative VA deltas should match placement deltas when the heap base is
  // linear (common for DEFAULT buffer heaps).
  if (vas[1] > vas[0]) {
    EXPECT_EQ(vas[1] - vas[0], offsets[1] - offsets[0]);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string GpuVaSizeName(const ::testing::TestParamInfo<GpuVaSizeCase> &info) {
  return "Size" + std::to_string(info.param.size);
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, GpuVaMatrixSpec,
                         ::testing::ValuesIn(BuildGpuVaSizeCases()),
                         GpuVaSizeName);

} // namespace
