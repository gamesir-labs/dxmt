#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class EnhancedBarrierSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
    const HRESULT options_hr = context_.device()->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options));
    if (options_hr == E_INVALIDARG)
      GTEST_SKIP() << "D3D12_OPTIONS12 is unavailable on this runtime";
    ASSERT_EQ(options_hr, S_OK);
    if (!options.EnhancedBarriersSupported)
      GTEST_SKIP() << "enhanced barriers are not advertised";
    ASSERT_EQ(
        context_.list()->QueryInterface(__uuidof(ID3D12GraphicsCommandList7),
                                        reinterpret_cast<void **>(list_.put())),
        S_OK);
    ASSERT_TRUE(list_);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12GraphicsCommandList7> list_;
};

TEST_F(EnhancedBarrierSpec, GlobalSyncAndAccessMatrixPreservesLaterWork) {
  struct BarrierCase {
    D3D12_BARRIER_SYNC before_sync;
    D3D12_BARRIER_SYNC after_sync;
    D3D12_BARRIER_ACCESS before_access;
    D3D12_BARRIER_ACCESS after_access;
  };
  constexpr std::array cases = {
      BarrierCase{D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_COPY,
                  D3D12_BARRIER_ACCESS_NO_ACCESS,
                  D3D12_BARRIER_ACCESS_COPY_DEST},
      BarrierCase{D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                  D3D12_BARRIER_ACCESS_COPY_DEST,
                  D3D12_BARRIER_ACCESS_UNORDERED_ACCESS},
      BarrierCase{D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                  D3D12_BARRIER_SYNC_PIXEL_SHADING,
                  D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
                  D3D12_BARRIER_ACCESS_SHADER_RESOURCE},
      BarrierCase{D3D12_BARRIER_SYNC_PIXEL_SHADING,
                  D3D12_BARRIER_SYNC_RENDER_TARGET,
                  D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                  D3D12_BARRIER_ACCESS_RENDER_TARGET},
      BarrierCase{D3D12_BARRIER_SYNC_RENDER_TARGET,
                  D3D12_BARRIER_SYNC_EXECUTE_INDIRECT,
                  D3D12_BARRIER_ACCESS_RENDER_TARGET,
                  D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT},
      BarrierCase{D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_ALL,
                  D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_ACCESS_COMMON},
  };

  for (const auto &test : cases) {
    const D3D12_GLOBAL_BARRIER barrier = {test.before_sync, test.after_sync,
                                          test.before_access,
                                          test.after_access};
    D3D12_BARRIER_GROUP group = {};
    group.Type = D3D12_BARRIER_TYPE_GLOBAL;
    group.NumBarriers = 1;
    group.pGlobalBarriers = &barrier;
    list_->Barrier(1, &group);
  }

  constexpr std::array<std::uint32_t, 4> expected = {0x10203040u, 0x50607080u,
                                                     0x90a0b0c0u, 0xd0e0f001u};
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                            sizeof(expected));
  auto destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);
  list_->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                          sizeof(expected));
  D3D12TestContext::Transition(list_.get(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(
      context_.ReadbackBuffer(destination.get(), sizeof(expected), &bytes),
      S_OK);
  ASSERT_EQ(bytes.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(bytes.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(EnhancedBarrierSpec, TextureLayoutTransitionsClearAndReadBack) {
  constexpr UINT kSize = 8;
  auto target = context_.CreateTexture2D(
      kSize, kSize, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(target);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

  D3D12_TEXTURE_BARRIER barrier = {};
  barrier.SyncBefore = D3D12_BARRIER_SYNC_NONE;
  barrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
  barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
  barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
  barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
  barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier.pResource = target.get();
  barrier.Subresources.IndexOrFirstMipLevel = std::numeric_limits<UINT>::max();
  D3D12_BARRIER_GROUP group = {};
  group.Type = D3D12_BARRIER_TYPE_TEXTURE;
  group.NumBarriers = 1;
  group.pTextureBarriers = &barrier;
  list_->Barrier(1, &group);

  constexpr FLOAT clear[4] = {0.125f, 0.25f, 0.5f, 1.0f};
  list_->ClearRenderTargetView(rtv, clear, 0, nullptr);

  barrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
  barrier.SyncAfter = D3D12_BARRIER_SYNC_COPY;
  barrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
  barrier.AccessAfter = D3D12_BARRIER_ACCESS_COPY_SOURCE;
  barrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  barrier.LayoutAfter = D3D12_BARRIER_LAYOUT_COPY_SOURCE;
  list_->Barrier(1, &group);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
  constexpr std::uint32_t expected = 0xff804020u;
  for (UINT y = 0; y < kSize; ++y) {
    for (UINT x = 0; x < kSize; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_TRUE(ColorsMatch(actual, expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(EnhancedBarrierSpec, BufferRangeAndDirectQueueLayoutsAreAccepted) {
  constexpr UINT64 kSize = 256;
  auto buffer = context_.CreateBuffer(kSize, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(buffer);

  std::array<D3D12_BUFFER_BARRIER, 2> barriers = {};
  barriers[0] = {D3D12_BARRIER_SYNC_NONE,
                 D3D12_BARRIER_SYNC_COPY,
                 D3D12_BARRIER_ACCESS_NO_ACCESS,
                 D3D12_BARRIER_ACCESS_COPY_DEST,
                 buffer.get(),
                 0,
                 128};
  barriers[1] = {D3D12_BARRIER_SYNC_NONE,
                 D3D12_BARRIER_SYNC_COPY,
                 D3D12_BARRIER_ACCESS_NO_ACCESS,
                 D3D12_BARRIER_ACCESS_COPY_DEST,
                 buffer.get(),
                 128,
                 128};
  D3D12_BARRIER_GROUP group = {};
  group.Type = D3D12_BARRIER_TYPE_BUFFER;
  group.NumBarriers = barriers.size();
  group.pBufferBarriers = barriers.data();
  list_->Barrier(1, &group);

  for (auto &barrier : barriers) {
    barrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
    barrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
    barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
  }
  list_->Barrier(1, &group);
  EXPECT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
