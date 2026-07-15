#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class LegacyBarrierSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  void TestEntryBoundary(UINT count) {
    std::vector<std::uint32_t> expected(count);
    std::vector<ComPtr<ID3D12Resource>> uploads(count);
    std::vector<ComPtr<ID3D12Resource>> resources(count);
    std::vector<D3D12_RESOURCE_BARRIER> barriers(count);
    for (UINT index = 0; index < count; ++index) {
      expected[index] = 0x10000000u + index * 0x010101u;
      uploads[index] = context_.CreateUploadBuffer(
          sizeof(expected[index]), &expected[index], sizeof(expected[index]));
      resources[index] = context_.CreateBuffer(
          sizeof(expected[index]), D3D12_HEAP_TYPE_DEFAULT,
          D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
      ASSERT_TRUE(uploads[index]);
      ASSERT_TRUE(resources[index]);
      context_.list()->CopyBufferRegion(
          resources[index].get(), 0, uploads[index].get(), 0,
          sizeof(expected[index]));
      barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[index].Transition.pResource = resources[index].get();
      barriers[index].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barriers[index].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barriers[index].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    context_.list()->ResourceBarrier(count, barriers.data());

    auto readback = context_.CreateBuffer(
        expected.size() * sizeof(expected[0]), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(readback);
    for (UINT index = 0; index < count; ++index) {
      context_.list()->CopyBufferRegion(
          readback.get(), index * sizeof(expected[index]),
          resources[index].get(), 0, sizeof(expected[index]));
    }
    ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

    void *mapping = nullptr;
    const D3D12_RANGE read_range = {0, expected.size() * sizeof(expected[0])};
    ASSERT_TRUE(SUCCEEDED(readback->Map(0, &read_range, &mapping)));
    EXPECT_EQ(std::memcmp(mapping, expected.data(), read_range.End), 0)
        << "barrier count " << count;
    const D3D12_RANGE written_range = {0, 0};
    readback->Unmap(0, &written_range);
  }

  D3D12TestContext context_;
};

TEST_F(LegacyBarrierSpec, EntryBoundary31) { TestEntryBoundary(31); }

TEST_F(LegacyBarrierSpec, EntryBoundary32) { TestEntryBoundary(32); }

TEST_F(LegacyBarrierSpec, EntryBoundary33) { TestEntryBoundary(33); }

TEST_F(LegacyBarrierSpec, EntryBoundary64) { TestEntryBoundary(64); }

TEST_F(LegacyBarrierSpec, EntryBoundary255) { TestEntryBoundary(255); }

TEST_F(LegacyBarrierSpec, EntryBoundary256) { TestEntryBoundary(256); }

TEST_F(LegacyBarrierSpec, EntryBoundary257) { TestEntryBoundary(257); }

} // namespace
