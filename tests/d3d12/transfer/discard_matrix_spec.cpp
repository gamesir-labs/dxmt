#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.9: Discard whole resource / subresource / rect, then overwrite oracle.
// Public D3D12 API only.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct DiscardRectCase {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
};

std::vector<DiscardRectCase> BuildDiscardRectCases() {
  return {
      {0, 0, 8, 6},   // whole
      {0, 0, 1, 1},   // single pixel
      {1, 1, 7, 5},   // interior
      {0, 0, 4, 3},   // top-left quadrant
      {4, 3, 8, 6},   // bottom-right quadrant
      {2, 0, 6, 6},   // vertical strip
      {0, 2, 8, 4},   // horizontal strip
  };
}

class DiscardMatrixSpec : public ::testing::TestWithParam<DiscardRectCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(DiscardMatrixSpec, DiscardRectThenClearProducesExpectedPixels) {
  constexpr UINT kWidth = 8;
  constexpr UINT kHeight = 6;
  const auto &rect_case = GetParam();
  auto texture = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);

  constexpr float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  constexpr float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  // Full clear establishes a known baseline after whole-resource discard.
  context_.list()->DiscardResource(texture.get(), nullptr);
  context_.list()->ClearRenderTargetView(rtv, blue, 0, nullptr);

  const D3D12_RECT rect = {rect_case.left, rect_case.top, rect_case.right,
                           rect_case.bottom};
  const D3D12_DISCARD_REGION region = {1, &rect, 0, 1};
  context_.list()->DiscardResource(texture.get(), &region);
  context_.list()->ClearRenderTargetView(rtv, green, 1, &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      const bool overwritten = x >= UINT(rect.left) && x < UINT(rect.right) &&
                               y >= UINT(rect.top) && y < UINT(rect.bottom);
      // ClearRenderTargetView writes R8G8B8A8; ColorsMatch tolerates channel layout.
      EXPECT_TRUE(ColorsMatch(pixel, overwritten ? 0xff00ff00 : 0xffff0000, 0))
          << "pixel (" << x << "," << y << ") rect=[" << rect.left << ","
          << rect.top << "," << rect.right << "," << rect.bottom << "]";
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(DiscardMatrixSpec, DiscardWholeResourceThenClearIsUniform) {
  constexpr UINT kWidth = 4;
  constexpr UINT kHeight = 4;
  auto texture = context_.CreateTexture2D(
      kWidth, kHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->DiscardResource(texture.get(), nullptr);
  context_.list()->ClearRenderTargetView(rtv, red, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback), S_OK);
  for (UINT y = 0; y < kHeight; ++y) {
    for (UINT x = 0; x < kWidth; ++x) {
      UINT pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xff0000ff, 0)) << x << "," << y;
    }
  }
}

std::string DiscardRectName(
    const ::testing::TestParamInfo<DiscardRectCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom);
}

INSTANTIATE_TEST_SUITE_P(RectMatrix, DiscardMatrixSpec,
                         ::testing::ValuesIn(BuildDiscardRectCases()),
                         DiscardRectName);

} // namespace
