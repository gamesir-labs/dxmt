#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class CopyResourceSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(CopyResourceSpec, CopiesEverySubresource) {
  constexpr UINT width = 8;
  constexpr UINT height = 4;
  constexpr UINT mip_count = 3;
  constexpr UINT array_size = 2;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = array_size;
  desc.MipLevels = mip_count;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  ComPtr<ID3D12Resource> source;
  ComPtr<ID3D12Resource> destination;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(source.put()))));
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(destination.put()))));
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  std::vector<std::vector<std::uint32_t>> expected(array_size * mip_count);
  for (UINT slice = 0; slice < array_size; ++slice) {
    for (UINT mip = 0; mip < mip_count; ++mip) {
      const UINT subresource = mip + slice * mip_count;
      const UINT mip_width = std::max(1u, width >> mip);
      const UINT mip_height = std::max(1u, height >> mip);
      auto &pixels = expected[subresource];
      pixels.resize(mip_width * mip_height);
      for (UINT y = 0; y < mip_height; ++y) {
        for (UINT x = 0; x < mip_width; ++x) {
          pixels[y * mip_width + x] =
              0xff000000u | (slice << 20) | (mip << 16) | (y << 8) | x;
        }
      }
      ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
          source.get(), pixels.data(), mip_width * sizeof(std::uint32_t),
          pixels.size() * sizeof(std::uint32_t), subresource)));
    }
  }

  D3D12TestContext::Transition(
      context_.list(), source.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  context_.list()->CopyResource(destination.get(), source.get());
  D3D12TestContext::Transition(
      context_.list(), destination.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  for (UINT subresource = 0; subresource < expected.size(); ++subresource) {
    if (subresource) {
      ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
    }
    TextureReadback readback;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(
        destination.get(), &readback, subresource)));
    const UINT mip = subresource % mip_count;
    EXPECT_EQ(readback.width, std::max(1u, width >> mip));
    EXPECT_EQ(readback.height, std::max(1u, height >> mip));
    for (UINT y = 0; y < readback.height; ++y) {
      EXPECT_EQ(std::memcmp(
                    readback.data.data() + y * readback.row_pitch,
                    expected[subresource].data() + y * readback.width,
                    readback.width * sizeof(std::uint32_t)),
                0)
          << "subresource " << subresource << ", row " << y;
    }
  }
}

} // namespace
