#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct BarrierEntryBoundaryCase {
  UINT entry_count;
  UINT transition_resource_count;
  UINT uav_entry_count;
  const char *name;
};

class LegacyBarrierEntryBoundaryExecutionSpec
    : public ::testing::TestWithParam<BarrierEntryBoundaryCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(LegacyBarrierEntryBoundaryExecutionSpec,
       MixedTransitionAndUavBatchPreservesEveryWrite) {
  constexpr UINT kWordsPerResource = 4;
  constexpr UINT kUavWordCount = 16;
  constexpr UINT kFirstUavValue = 0x10203040u;
  constexpr UINT kFinalUavValue = 0xa1b2c3d4u;
  const auto parameter = GetParam();
  ASSERT_GT(parameter.transition_resource_count, 0u);
  ASSERT_GT(parameter.entry_count, 1u);
  ASSERT_LT(parameter.uav_entry_count, parameter.entry_count - 1);
  const UINT transition_entry_count =
      parameter.entry_count - parameter.uav_entry_count;
  ASSERT_EQ(transition_entry_count % parameter.transition_resource_count, 0u);
  const UINT transitions_per_resource =
      transition_entry_count / parameter.transition_resource_count;
  ASSERT_EQ(transitions_per_resource & 1u, 1u)
      << "each resource must end in COPY_SOURCE";

  std::vector<std::array<std::uint32_t, kWordsPerResource>> expected(
      parameter.transition_resource_count);
  std::vector<ComPtr<ID3D12Resource>> uploads(
      parameter.transition_resource_count);
  std::vector<ComPtr<ID3D12Resource>> resources(
      parameter.transition_resource_count);
  for (UINT resource = 0; resource < parameter.transition_resource_count;
       ++resource) {
    for (UINT word = 0; word < kWordsPerResource; ++word) {
      expected[resource][word] =
          0x60000000u ^ (parameter.entry_count << 16) ^
          (resource * 0x01010101u) ^ (word * 0x11111111u);
    }
    uploads[resource] = context_.CreateUploadBuffer(
        sizeof(expected[resource]), expected[resource].data(),
        sizeof(expected[resource]));
    resources[resource] = context_.CreateBuffer(
        sizeof(expected[resource]), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(uploads[resource]);
    ASSERT_TRUE(resources[resource]);
    context_.list()->CopyBufferRegion(
        resources[resource].get(), 0, uploads[resource].get(), 0,
        sizeof(expected[resource]));
  }

  auto uav_resource = context_.CreateBuffer(
      UINT64(kUavWordCount) * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto uav_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(uav_resource);
  ASSERT_TRUE(uav_heap);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_UINT;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kUavWordCount;
  context_.device()->CreateUnorderedAccessView(
      uav_resource.get(), nullptr, &uav,
      uav_heap->GetCPUDescriptorHandleForHeapStart());
  ID3D12DescriptorHeap *bound_heaps[] = {uav_heap.get()};
  context_.list()->SetDescriptorHeaps(1, bound_heaps);
  const std::array<UINT, 4> first_clear = {kFirstUavValue, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      uav_heap->GetGPUDescriptorHandleForHeapStart(),
      uav_heap->GetCPUDescriptorHandleForHeapStart(), uav_resource.get(),
      first_clear.data(), 0, nullptr);

  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  barriers.reserve(parameter.entry_count);
  std::vector<D3D12_RESOURCE_STATES> states(
      parameter.transition_resource_count, D3D12_RESOURCE_STATE_COPY_DEST);
  UINT emitted_uav_entries = 0;
  UINT emitted_transition_entries = 0;
  for (UINT slot = 0; slot < parameter.entry_count; ++slot) {
    // Emit the final UAV barrier by the penultimate slot. The last array entry
    // is then a state-changing transition to COPY_SOURCE, so truncating a
    // boundary-sized barrier array cannot silently satisfy the copy oracle.
    const UINT target_uav_entries =
        UINT((UINT64(slot + 1) * parameter.uav_entry_count) /
             (parameter.entry_count - 1));
    if (target_uav_entries > emitted_uav_entries) {
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrier.UAV.pResource = uav_resource.get();
      barriers.push_back(barrier);
      ++emitted_uav_entries;
      continue;
    }

    const UINT resource = emitted_transition_entries %
                          parameter.transition_resource_count;
    const auto before = states[resource];
    const auto after = before == D3D12_RESOURCE_STATE_COPY_DEST
                           ? D3D12_RESOURCE_STATE_COPY_SOURCE
                           : D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resources[resource].get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barriers.push_back(barrier);
    states[resource] = after;
    ++emitted_transition_entries;
  }
  ASSERT_EQ(barriers.size(), parameter.entry_count);
  ASSERT_EQ(barriers.back().Type, D3D12_RESOURCE_BARRIER_TYPE_TRANSITION);
  ASSERT_EQ(emitted_uav_entries, parameter.uav_entry_count);
  ASSERT_EQ(emitted_transition_entries, transition_entry_count);
  for (const auto state : states)
    ASSERT_EQ(state, D3D12_RESOURCE_STATE_COPY_SOURCE);

  context_.list()->ResourceBarrier(static_cast<UINT>(barriers.size()),
                                   barriers.data());
  const std::array<UINT, 4> final_clear = {kFinalUavValue, 0, 0, 0};
  context_.list()->ClearUnorderedAccessViewUint(
      uav_heap->GetGPUDescriptorHandleForHeapStart(),
      uav_heap->GetCPUDescriptorHandleForHeapStart(), uav_resource.get(),
      final_clear.data(), 0, nullptr);
  D3D12TestContext::Transition(
      context_.list(), uav_resource.get(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  const UINT64 transition_bytes =
      UINT64(parameter.transition_resource_count) * kWordsPerResource *
      sizeof(UINT);
  const UINT64 uav_bytes = UINT64(kUavWordCount) * sizeof(UINT);
  auto readback = context_.CreateBuffer(
      transition_bytes + uav_bytes, D3D12_HEAP_TYPE_READBACK,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(readback);
  for (UINT resource = 0; resource < parameter.transition_resource_count;
       ++resource) {
    context_.list()->CopyBufferRegion(
        readback.get(), UINT64(resource) * sizeof(expected[resource]),
        resources[resource].get(), 0, sizeof(expected[resource]));
  }
  context_.list()->CopyBufferRegion(readback.get(), transition_bytes,
                                    uav_resource.get(), 0, uav_bytes);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {
      0, static_cast<SIZE_T>(transition_bytes + uav_bytes)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  const auto *actual = static_cast<const std::uint32_t *>(mapping);
  for (UINT resource = 0; resource < parameter.transition_resource_count;
       ++resource) {
    for (UINT word = 0; word < kWordsPerResource; ++word) {
      const UINT index = resource * kWordsPerResource + word;
      EXPECT_EQ(actual[index], expected[resource][word])
          << "entry_count=" << parameter.entry_count
          << ", resource=" << resource << ", word=" << word;
    }
  }
  const UINT uav_word_offset =
      parameter.transition_resource_count * kWordsPerResource;
  for (UINT word = 0; word < kUavWordCount; ++word) {
    EXPECT_EQ(actual[uav_word_offset + word], kFinalUavValue)
        << "entry_count=" << parameter.entry_count
        << ", UAV word=" << word;
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

constexpr std::array<BarrierEntryBoundaryCase, 4> kBarrierEntryCases = {{
    {64, 4, 4, "Count64"},
    {255, 3, 6, "Count255"},
    {256, 4, 4, "Count256"},
    {257, 3, 8, "Count257"},
}};

INSTANTIATE_TEST_SUITE_P(
    EntryCount64_255_256_257, LegacyBarrierEntryBoundaryExecutionSpec,
    ::testing::ValuesIn(kBarrierEntryCases),
    [](const ::testing::TestParamInfo<BarrierEntryBoundaryCase> &info) {
      return std::string(info.param.name);
    });

struct SplitMipSubmissionCase {
  bool separate_execute_calls;
  const char *name;
};

class SplitMipExecutionSpec
    : public ::testing::TestWithParam<SplitMipSubmissionCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(SplitMipExecutionSpec,
       LastMipSplitPreservesIndependentMipZeroAcrossCommandLists) {
  constexpr UINT kWidth = 16;
  constexpr UINT kHeight = 8;
  constexpr UINT16 kMipLevels = 4;
  constexpr UINT kLastMip = kMipLevels - 1;
  constexpr UINT kLastMipWidth = kWidth >> kLastMip;
  constexpr UINT kLastMipHeight = kHeight >> kLastMip;
  static_assert(kLastMipWidth == 2);
  static_assert(kLastMipHeight == 1);

  std::array<std::uint32_t, kWidth * kHeight> mip_zero = {};
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      mip_zero[y * kWidth + x] =
          0x11000000u | (y << 12) | (x << 4) | ((x + y) & 0xfu);
    }
  }
  constexpr std::array<std::uint32_t, kLastMipWidth * kLastMipHeight>
      last_mip = {0xa1b2c3d4u, 0x55667788u};

  auto texture = context_.CreateTexture2D(
      kWidth, kHeight, kMipLevels, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), mip_zero.data(), kWidth * sizeof(UINT),
                sizeof(mip_zero), 0),
            S_OK);
  ASSERT_EQ(context_.UploadTextureAndReset(
                texture.get(), last_mip.data(),
                kLastMipWidth * sizeof(UINT), sizeof(last_mip), kLastMip),
            S_OK);

  const auto texture_desc = texture->GetDesc();
  std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kMipLevels> footprints = {};
  std::array<UINT, kMipLevels> row_counts = {};
  std::array<UINT64, kMipLevels> row_sizes = {};
  UINT64 total_bytes = 0;
  context_.device()->GetCopyableFootprints(
      &texture_desc, 0, kMipLevels, 0, footprints.data(), row_counts.data(),
      row_sizes.data(), &total_bytes);
  ASSERT_GE(row_sizes[0], UINT64(kWidth) * sizeof(UINT));
  ASSERT_GE(row_sizes[kLastMip],
            UINT64(kLastMipWidth) * sizeof(UINT));
  ASSERT_EQ(row_counts[0], kHeight);
  ASSERT_EQ(row_counts[kLastMip], kLastMipHeight);
  auto readback = context_.CreateBuffer(
      total_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(readback);

  std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
  std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
  for (UINT index = 0; index < lists.size(); ++index) {
    ASSERT_EQ(context_.device()->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  IID_PPV_ARGS(allocators[index].put())),
              S_OK);
    ASSERT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                  allocators[index].get(), nullptr,
                  IID_PPV_ARGS(lists[index].put())),
              S_OK);
  }

  D3D12_RESOURCE_BARRIER split = {};
  split.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  split.Transition.pResource = texture.get();
  split.Transition.Subresource = kLastMip;
  split.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  split.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  split.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
  lists[0]->ResourceBarrier(1, &split);

  // Mip zero is independent of the last mip's pending split transition. Use
  // it before the END_ONLY barrier to prove it remains available and intact.
  D3D12_RESOURCE_BARRIER mip_zero_transition = {};
  mip_zero_transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  mip_zero_transition.Transition.pResource = texture.get();
  mip_zero_transition.Transition.Subresource = 0;
  mip_zero_transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  mip_zero_transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  lists[0]->ResourceBarrier(1, &mip_zero_transition);

  D3D12_TEXTURE_COPY_LOCATION mip_zero_source = {};
  mip_zero_source.pResource = texture.get();
  mip_zero_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  mip_zero_source.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION mip_zero_destination = {};
  mip_zero_destination.pResource = readback.get();
  mip_zero_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  mip_zero_destination.PlacedFootprint = footprints[0];
  lists[0]->CopyTextureRegion(&mip_zero_destination, 0, 0, 0,
                              &mip_zero_source, nullptr);
  ASSERT_EQ(lists[0]->Close(), S_OK);

  split.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  lists[1]->ResourceBarrier(1, &split);
  D3D12_TEXTURE_COPY_LOCATION last_mip_source = {};
  last_mip_source.pResource = texture.get();
  last_mip_source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  last_mip_source.SubresourceIndex = kLastMip;
  D3D12_TEXTURE_COPY_LOCATION last_mip_destination = {};
  last_mip_destination.pResource = readback.get();
  last_mip_destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  last_mip_destination.PlacedFootprint = footprints[kLastMip];
  lists[1]->CopyTextureRegion(&last_mip_destination, 0, 0, 0,
                              &last_mip_source, nullptr);
  ASSERT_EQ(lists[1]->Close(), S_OK);

  ID3D12CommandList *submissions[] = {lists[0].get(), lists[1].get()};
  if (GetParam().separate_execute_calls) {
    context_.queue()->ExecuteCommandLists(1, &submissions[0]);
    ASSERT_EQ(context_.SignalAndWait(), S_OK);
    context_.queue()->ExecuteCommandLists(1, &submissions[1]);
  } else {
    context_.queue()->ExecuteCommandLists(2, submissions);
  }
  ASSERT_EQ(context_.SignalAndWait(), S_OK);

  void *mapping = nullptr;
  const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(total_bytes)};
  ASSERT_EQ(readback->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  const auto *bytes = static_cast<const std::uint8_t *>(mapping);
  for (UINT y = 0; y < kHeight; ++y) {
    EXPECT_EQ(std::memcmp(bytes + footprints[0].Offset +
                              y * footprints[0].Footprint.RowPitch,
                          mip_zero.data() + y * kWidth,
                          kWidth * sizeof(UINT)),
              0)
        << "mip 0 row " << y;
  }
  for (UINT y = 0; y < kLastMipHeight; ++y) {
    EXPECT_EQ(std::memcmp(bytes + footprints[kLastMip].Offset +
                              y * footprints[kLastMip].Footprint.RowPitch,
                          last_mip.data() + y * kLastMipWidth,
                          kLastMipWidth * sizeof(UINT)),
              0)
        << "last mip row " << y;
  }
  const D3D12_RANGE no_write = {};
  readback->Unmap(0, &no_write);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

constexpr std::array<SplitMipSubmissionCase, 2> kSplitMipCases = {{
    {false, "SingleExecuteCall"},
    {true, "SeparateExecuteCalls"},
}};

INSTANTIATE_TEST_SUITE_P(
    AcrossListsAndExecuteCalls, SplitMipExecutionSpec,
    ::testing::ValuesIn(kSplitMipCases),
    [](const ::testing::TestParamInfo<SplitMipSubmissionCase> &info) {
      return std::string(info.param.name);
    });

} // namespace
