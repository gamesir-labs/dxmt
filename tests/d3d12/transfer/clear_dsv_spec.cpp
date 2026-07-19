#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class ClearDsvSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  bool Supports(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    return SUCCEEDED(context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
           (support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
  }

  D3D12_RESOURCE_DESC TextureDesc(DXGI_FORMAT format, UINT width, UINT height,
                                  UINT16 array_size = 1) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = array_size;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    return desc;
  }

  ComPtr<ID3D12Resource> CreateTexture(DXGI_FORMAT format, UINT width = 4,
                                       UINT height = 4, UINT16 array_size = 1) {
    const auto desc = TextureDesc(format, width, height, array_size);
    D3D12_HEAP_PROPERTIES heap_properties = {};
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> resource;
    HRESULT hr = context_.device()->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
        IID_PPV_ARGS(resource.put()));
    return SUCCEEDED(hr) ? std::move(resource) : ComPtr<ID3D12Resource>();
  }

  float Depth32(const TextureReadback &readback, UINT x, UINT y) {
    float value = 0.0f;
    std::memcpy(&value,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(value),
                sizeof(value));
    return value;
  }

  float Depth24(const TextureReadback &readback, UINT x, UINT y) {
    std::uint32_t value = 0;
    std::memcpy(&value,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(value),
                sizeof(value));
    return float(value & 0x00ffffffu) / float(0x00ffffffu);
  }

  std::uint16_t Depth16(const TextureReadback &readback, UINT x, UINT y) {
    std::uint16_t value = 0;
    std::memcpy(&value,
                readback.data.data() + y * readback.row_pitch +
                    x * sizeof(value),
                sizeof(value));
    return value;
  }

  std::uint8_t Stencil(const TextureReadback &readback, UINT x, UINT y) {
    return readback.data[y * readback.row_pitch + x];
  }

  void ExpectDepth32Solid(const TextureReadback &readback, float expected) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        EXPECT_NEAR(Depth32(readback, x, y), expected, 1.0e-6f)
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  void ExpectStencilSolid(const TextureReadback &readback,
                          std::uint8_t expected) {
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        EXPECT_EQ(Stencil(readback, x, y), expected)
            << "pixel (" << x << ", " << y << ")";
      }
    }
  }

  HRESULT ReadbackAndReset(ID3D12Resource *texture, TextureReadback *readback,
                           UINT subresource = 0) {
    HRESULT hr = context_.ReadbackTexture(texture, readback, subresource);
    return FAILED(hr) ? hr : context_.ResetCommandList();
  }

  D3D12TestContext context_;
};

TEST_F(ClearDsvSpec, ClearsDepthOnlyWithoutChangingStencil) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT_S8X24_UINT is unavailable";
  auto texture = CreateTexture(format);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 17, 0,
      nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.75f, 99,
                                         0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback depth;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &depth, 0)));
  ExpectDepth32Solid(depth, 0.75f);
  TextureReadback stencil;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &stencil, 1)));
  ExpectStencilSolid(stencil, 17);
}

TEST_F(ClearDsvSpec, ClearsStencilOnlyWithoutChangingDepth) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT_S8X24_UINT is unavailable";
  auto texture = CreateTexture(format);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 17, 0,
      nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 0.9f,
                                         231, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback depth;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &depth, 0)));
  ExpectDepth32Solid(depth, 0.25f);
  TextureReadback stencil;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &stencil, 1)));
  ExpectStencilSolid(stencil, 231);
}

TEST_F(ClearDsvSpec, ClearsDepthAndStencilTogether) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT_S8X24_UINT is unavailable";
  auto texture = CreateTexture(format);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.625f, 0xa5, 0,
      nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback depth;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &depth, 0)));
  ExpectDepth32Solid(depth, 0.625f);
  TextureReadback stencil;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &stencil, 1)));
  ExpectStencilSolid(stencil, 0xa5);
}

TEST_F(ClearDsvSpec, ClearsSingleDepthRectWithoutAffectingOutside) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT is unavailable";
  auto texture = CreateTexture(format, 8, 6);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  constexpr D3D12_RECT rect = {2, 1, 6, 5};
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0,
                                         0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0,
                                         1, &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const float expected = x >= 2 && x < 6 && y >= 1 && y < 5 ? 0.25f : 1.0f;
      EXPECT_NEAR(Depth32(readback, x, y), expected, 1.0e-6f)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearDsvSpec, ClearsDepthAndStencilRectWithoutAffectingOutside) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT_S8X24_UINT is unavailable";
  auto texture = CreateTexture(format, 6, 4);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  constexpr D3D12_RECT rect = {1, 1, 5, 3};
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 3, 0,
      nullptr);
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.25f, 0x7f, 1,
      &rect);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback depth;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &depth, 0)));
  TextureReadback stencil;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &stencil, 1)));
  for (UINT y = 0; y < depth.height; ++y) {
    for (UINT x = 0; x < depth.width; ++x) {
      const bool inside = x >= 1 && x < 5 && y >= 1 && y < 3;
      EXPECT_NEAR(Depth32(depth, x, y), inside ? 0.25f : 1.0f, 1.0e-6f)
          << "depth pixel (" << x << ", " << y << ")";
      EXPECT_EQ(Stencil(stencil, x, y), inside ? 0x7f : 3)
          << "stencil pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearDsvSpec, ClearsMultipleDepthRects) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT is unavailable";
  auto texture = CreateTexture(format, 8, 4);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  constexpr std::array<D3D12_RECT, 2> rects = {D3D12_RECT{0, 0, 2, 4},
                                               D3D12_RECT{6, 0, 8, 4}};
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0,
                                         0, nullptr);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0,
                                         static_cast<UINT>(rects.size()),
                                         rects.data());
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      const float expected = x < 2 || x >= 6 ? 0.5f : 1.0f;
      EXPECT_NEAR(Depth32(readback, x, y), expected, 1.0e-6f)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearDsvSpec, ClearsOnlySelectedArraySlice) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT is unavailable";
  auto texture = CreateTexture(format, 4, 4, 2);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
  desc.Format = format;
  desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
  desc.Texture2DArray.ArraySize = 1;
  const auto first_dsv = context_.CpuDescriptorHandle(heap.get(), 0);
  context_.device()->CreateDepthStencilView(texture.get(), &desc, first_dsv);
  desc.Texture2DArray.FirstArraySlice = 1;
  const auto second_dsv = context_.CpuDescriptorHandle(heap.get(), 1);
  context_.device()->CreateDepthStencilView(texture.get(), &desc, second_dsv);
  context_.list()->ClearDepthStencilView(first_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.25f, 0, 0, nullptr);
  context_.list()->ClearDepthStencilView(second_dsv, D3D12_CLEAR_FLAG_DEPTH,
                                         0.75f, 0, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback first;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &first, 0)));
  ExpectDepth32Solid(first, 0.25f);
  TextureReadback second;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &second, 1)));
  ExpectDepth32Solid(second, 0.75f);
}

TEST_F(ClearDsvSpec, ClearsD16Unorm) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D16_UNORM;
  if (!Supports(format))
    GTEST_SKIP() << "D16_UNORM is unavailable";
  auto texture = CreateTexture(format);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0,
                                         0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      EXPECT_NEAR(Depth16(readback, x, y), 32768, 1)
          << "pixel (" << x << ", " << y << ")";
    }
  }
}

TEST_F(ClearDsvSpec, ClearsD24S8) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D24_UNORM_S8_UINT is unavailable";
  auto texture = CreateTexture(format);
  auto heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(heap);
  const auto dsv = heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(
      dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.375f, 0xe7, 0,
      nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback depth;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &depth, 0)));
  for (UINT y = 0; y < depth.height; ++y) {
    for (UINT x = 0; x < depth.width; ++x) {
      EXPECT_NEAR(Depth24(depth, x, y), 0.375f, 1.0f / 0x00ffffffu)
          << "pixel (" << x << ", " << y << ")";
    }
  }
  TextureReadback stencil;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &stencil, 1)));
  ExpectStencilSolid(stencil, 0xe7);
}

TEST_F(ClearDsvSpec, ClearsDepthBoundaryAndFractionalValues) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT is unavailable";
  constexpr std::array<float, 3> values = {0.0f, 0.375f, 1.0f};
  std::array<ComPtr<ID3D12Resource>, values.size()> textures;
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                            values.size(), false);
  ASSERT_TRUE(heap);
  for (UINT index = 0; index < values.size(); ++index) {
    textures[index] = CreateTexture(format, 2, 2);
    ASSERT_TRUE(textures[index]);
    const auto dsv = context_.CpuDescriptorHandle(heap.get(), index);
    context_.device()->CreateDepthStencilView(textures[index].get(), nullptr,
                                              dsv);
    context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH,
                                           values[index], 0, 0, nullptr);
    D3D12TestContext::Transition(context_.list(), textures[index].get(),
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
  }

  for (UINT index = 0; index < values.size(); ++index) {
    TextureReadback readback;
    ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(textures[index].get(), &readback)));
    ExpectDepth32Solid(readback, values[index]);
  }
}

TEST_F(ClearDsvSpec, ClearsStencilBoundaryValues) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT_S8X24_UINT is unavailable";
  constexpr std::array<std::uint8_t, 2> values = {0, 255};
  std::array<ComPtr<ID3D12Resource>, values.size()> textures;
  auto heap = context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                            values.size(), false);
  ASSERT_TRUE(heap);
  for (UINT index = 0; index < values.size(); ++index) {
    textures[index] = CreateTexture(format, 2, 2);
    ASSERT_TRUE(textures[index]);
    const auto dsv = context_.CpuDescriptorHandle(heap.get(), index);
    context_.device()->CreateDepthStencilView(textures[index].get(), nullptr,
                                              dsv);
    context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 0.0f,
                                           values[index], 0, nullptr);
    D3D12TestContext::Transition(context_.list(), textures[index].get(),
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
  }

  for (UINT index = 0; index < values.size(); ++index) {
    TextureReadback readback;
    ASSERT_TRUE(
        SUCCEEDED(ReadbackAndReset(textures[index].get(), &readback, 1)));
    ExpectStencilSolid(readback, values[index]);
  }
}

TEST_F(ClearDsvSpec, ClearsPlacedDepthStencilResource) {
  constexpr DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
  if (!Supports(format))
    GTEST_SKIP() << "D32_FLOAT is unavailable";
  const auto desc = TextureDesc(format, 4, 4);
  const auto allocation =
      context_.device()->GetResourceAllocationInfo(0, 1, &desc);
  ASSERT_NE(allocation.SizeInBytes, 0u);
  ASSERT_NE(allocation.SizeInBytes, UINT64_MAX);
  D3D12_HEAP_DESC heap_desc = {};
  heap_desc.SizeInBytes = allocation.SizeInBytes;
  heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_desc.Alignment = allocation.Alignment;
  heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
  ComPtr<ID3D12Heap> resource_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateHeap(
      &heap_desc, IID_PPV_ARGS(resource_heap.put()))));
  ComPtr<ID3D12Resource> texture;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreatePlacedResource(
      resource_heap.get(), 0, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
      IID_PPV_ARGS(texture.put()))));
  auto descriptor_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(descriptor_heap);
  const auto dsv = descriptor_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);
  context_.list()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.375f, 0,
                                         0, nullptr);
  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_DEPTH_WRITE,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(ReadbackAndReset(texture.get(), &readback)));
  ExpectDepth32Solid(readback, 0.375f);
}


} // namespace
