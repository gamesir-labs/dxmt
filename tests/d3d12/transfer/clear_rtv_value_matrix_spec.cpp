#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.5: ClearRenderTargetView color and rect matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct ClearRtvColorCase {
  float r, g, b, a;
  std::uint32_t expected_bgra_approx; // ColorsMatch handles channel order
};

std::vector<ClearRtvColorCase> BuildClearRtvColorCases() {
  return {
      {1, 0, 0, 1, 0xff0000ff}, {0, 1, 0, 1, 0xff00ff00}, {0, 0, 1, 1, 0xffff0000},
      {0, 0, 0, 1, 0xff000000}, {1, 1, 1, 1, 0xffffffff},
      {0.5f, 0.5f, 0.5f, 1, 0xff808080}, {1, 1, 0, 1, 0xff00ffff},
      {0, 1, 1, 1, 0xffffff00}, {1, 0, 1, 1, 0xffff00ff},
      {0.25f, 0.5f, 0.75f, 1, 0xffbf8040}, {0.125f, 0, 0, 1, 0xff000020},
      {0, 0, 0, 0, 0x00000000}, {1, 0, 0, 0.5f, 0x800000ff},
  };
}

struct ClearRtvRectCase {
  LONG left, top, right, bottom;
};

std::vector<ClearRtvRectCase> BuildClearRtvRectCases() {
  return {
      {0, 0, 8, 6}, {0, 0, 1, 1}, {1, 1, 7, 5}, {0, 0, 4, 3},
      {4, 3, 8, 6}, {2, 0, 6, 6}, {0, 2, 8, 4}, {3, 2, 5, 4},
  };
}

class ClearRtvColorMatrixSpec
    : public ::testing::TestWithParam<ClearRtvColorCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(ClearRtvColorMatrixSpec, WholeTargetMatchesColor) {
  const auto &test = GetParam();
  constexpr UINT kW = 8, kH = 8;
  auto texture = context_.CreateTexture2D(
      kW, kH, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  const FLOAT color[4] = {test.r, test.g, test.b, test.a};
  context_.list()->ClearRenderTargetView(rtv, color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback), S_OK);
  for (UINT y = 0; y < kH; ++y) {
    for (UINT x = 0; x < kW; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, test.expected_bgra_approx, 2))
          << "pixel (" << x << "," << y << ") rgba=(" << test.r << "," << test.g
          << "," << test.b << "," << test.a << ")";
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

class ClearRtvRectMatrixSpec
    : public ::testing::TestWithParam<ClearRtvRectCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }
  D3D12TestContext context_;
};

TEST_P(ClearRtvRectMatrixSpec, RectClearDoesNotAffectOutside) {
  const auto &rect_case = GetParam();
  constexpr UINT kW = 8, kH = 6;
  auto texture = context_.CreateTexture2D(
      kW, kH, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT blue[4] = {0, 0, 1, 1};
  constexpr FLOAT red[4] = {1, 0, 0, 1};
  const D3D12_RECT rect = {rect_case.left, rect_case.top, rect_case.right,
                           rect_case.bottom};
  context_.list()->ClearRenderTargetView(rtv, blue, 0, nullptr);
  context_.list()->ClearRenderTargetView(rtv, red, 1, &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback), S_OK);
  for (UINT y = 0; y < kH; ++y) {
    for (UINT x = 0; x < kW; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      const bool inside = x >= UINT(rect.left) && x < UINT(rect.right) &&
                          y >= UINT(rect.top) && y < UINT(rect.bottom);
      EXPECT_TRUE(ColorsMatch(pixel, inside ? 0xff0000ff : 0xffff0000, 1))
          << "pixel (" << x << "," << y << ")";
    }
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ColorName(const ::testing::TestParamInfo<ClearRtvColorCase> &info) {
  return "R" + std::to_string(int(info.param.r * 1000)) + "G" +
         std::to_string(int(info.param.g * 1000)) + "B" +
         std::to_string(int(info.param.b * 1000)) + "A" +
         std::to_string(int(info.param.a * 1000)) + "I" +
         std::to_string(info.index);
}

std::string RectName(const ::testing::TestParamInfo<ClearRtvRectCase> &info) {
  return "L" + std::to_string(info.param.left) + "T" +
         std::to_string(info.param.top) + "R" +
         std::to_string(info.param.right) + "B" +
         std::to_string(info.param.bottom);
}

INSTANTIATE_TEST_SUITE_P(ColorMatrix, ClearRtvColorMatrixSpec,
                         ::testing::ValuesIn(BuildClearRtvColorCases()),
                         ColorName);
INSTANTIATE_TEST_SUITE_P(RectMatrix, ClearRtvRectMatrixSpec,
                         ::testing::ValuesIn(BuildClearRtvRectCases()),
                         RectName);

} // namespace
