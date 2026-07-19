#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class ClearRtvSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ComPtr<ID3D12Resource>
  CreateRenderTarget(UINT width, UINT height, UINT16 mip_levels = 1,
                     UINT16 array_size = 1,
                     DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = array_size;
    desc.MipLevels = mip_levels;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = context_.device()->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
        IID_PPV_ARGS(resource.put()));
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
  }

  std::uint32_t Pixel(const TextureReadback &readback, UINT x, UINT y) {
    std::uint32_t pixel = 0;
    std::memcpy(&pixel,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(pixel),
                sizeof(pixel));
    return pixel;
  }

  void ExpectSolid(const TextureReadback &readback, std::uint32_t expected) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        EXPECT_TRUE(ColorsMatch(Pixel(readback, x, y), expected, 1))
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  D3D12TestContext context_;
};

TEST_F(ClearRtvSpec, ClearsWholeRenderTarget) {
  auto texture = CreateRenderTarget(8, 8);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT color[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ExpectSolid(readback, 0xff0000ff);
}

TEST_F(ClearRtvSpec, ClearsSingleRectWithoutAffectingOutside) {
  auto texture = CreateRenderTarget(8, 6);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  constexpr FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr D3D12_RECT rect = {2, 1, 6, 5};
  context_.list()->ClearRenderTargetView(rtv, blue, 0, nullptr);
  context_.list()->ClearRenderTargetView(rtv, red, 1, &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const bool inside = x >= 2 && x < 6 && y >= 1 && y < 5;
      const std::uint32_t expected = inside ? 0xff0000ff : 0xffff0000;
      EXPECT_TRUE(ColorsMatch(Pixel(readback, x, y), expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearRtvSpec, ClearsMultipleRects) {
  auto texture = CreateRenderTarget(8, 4);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT black[4] = {};
  constexpr FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr std::array<D3D12_RECT, 2> rects = {D3D12_RECT{0, 0, 2, 4},
                                               D3D12_RECT{6, 0, 8, 4}};
  context_.list()->ClearRenderTargetView(rtv, black, 0, nullptr);
  context_.list()->ClearRenderTargetView(
      rtv, green, static_cast<UINT>(rects.size()), rects.data());
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const std::uint32_t expected = x < 2 || x >= 6 ? 0xff00ff00 : 0;
      EXPECT_TRUE(ColorsMatch(Pixel(readback, x, y), expected, 1))
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearRtvSpec, ZeroRectCountClearsWholeTarget) {
  auto texture = CreateRenderTarget(4, 4);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  constexpr D3D12_RECT ignored = {0, 0, 0, 0};
  context_.list()->ClearRenderTargetView(rtv, green, 0, &ignored);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ExpectSolid(readback, 0xff00ff00);
}

TEST_F(ClearRtvSpec, ClearsNonzeroMip) {
  auto texture = CreateRenderTarget(8, 8, 3);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  desc.Texture2D.MipSlice = 2;
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), &desc, rtv);
  constexpr FLOAT color[4] = {1.0f, 0.5f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback, 2)));
  ASSERT_EQ(readback.width, 2u);
  ASSERT_EQ(readback.height, 2u);
  ExpectSolid(readback, 0xff0080ff);
}

TEST_F(ClearRtvSpec, ClearsOnlySelectedArraySlice) {
  auto texture = CreateRenderTarget(4, 4, 1, 2);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_RENDER_TARGET_VIEW_DESC desc = {};
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.ArraySize = 1;
  desc.Texture2DArray.FirstArraySlice = 0;
  const auto first_rtv = context_.CpuDescriptorHandle(heap.get(), 0);
  context_.device()->CreateRenderTargetView(texture.get(), &desc, first_rtv);
  desc.Texture2DArray.FirstArraySlice = 1;
  const auto second_rtv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateRenderTargetView(texture.get(), &desc, second_rtv);
  constexpr FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  constexpr FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  context_.list()->ClearRenderTargetView(first_rtv, red, 0, nullptr);
  context_.list()->ClearRenderTargetView(second_rtv, green, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback first;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &first, 0)));
  ExpectSolid(first, 0xff0000ff);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  TextureReadback second;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &second, 1)));
  ExpectSolid(second, 0xff00ff00);
}

TEST_F(ClearRtvSpec, ConvertsAndClampsUnormColor) {
  auto texture = CreateRenderTarget(2, 2);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr FLOAT color[4] = {-0.5f, 0.5f, 1.5f, 0.25f};
  context_.list()->ClearRenderTargetView(rtv, color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ExpectSolid(readback, 0x40ff8000);
}

TEST_F(ClearRtvSpec, PreservesFloatColorComponents) {
  D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
  support.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CheckFeatureSupport(
      D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))));
  if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    GTEST_SKIP() << "R32G32B32A32_FLOAT render targets are unavailable";
  auto texture = CreateRenderTarget(1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(texture.get(), nullptr, rtv);
  constexpr std::array<FLOAT, 4> expected = {0.25f, -2.0f, 100.0f, 1.0f};
  context_.list()->ClearRenderTargetView(rtv, expected.data(), 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  std::array<FLOAT, 4> actual = {};
  std::memcpy(actual.data(), readback.data.data(), sizeof(actual));
  for (std::size_t index = 0; index < expected.size(); ++index)
    EXPECT_FLOAT_EQ(actual[index], expected[index]) << "component " << index;
}


} // namespace
