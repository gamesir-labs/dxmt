#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 Texture2D CreateCommittedResource size/format matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct Texture2DSizeCase {
  UINT width;
  UINT height;
  UINT16 mips;
  DXGI_FORMAT format;
  D3D12_RESOURCE_FLAGS flags;
};

std::vector<Texture2DSizeCase> BuildTexture2DSizeCases() {
  std::vector<Texture2DSizeCase> cases;
  const UINT dims[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64,
                       127, 128, 256, 512, 1024};
  const DXGI_FORMAT formats[] = {
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_UINT,
      DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R8_UINT};
  for (const UINT w : dims) {
    for (const UINT h : {1u, 2u, w}) {
      if (h > 1024)
        continue;
      for (const DXGI_FORMAT format : formats) {
        cases.push_back({w, h, 1, format, D3D12_RESOURCE_FLAG_NONE});
        cases.push_back({w, h, 1, format,
                         D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET});
        if (format == DXGI_FORMAT_R32_FLOAT ||
            format == DXGI_FORMAT_R8G8B8A8_UNORM)
          cases.push_back({w, h, 1, format,
                           D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS});
      }
    }
  }
  // Mip chains for powers of two.
  for (UINT size : {4u, 8u, 16u, 32u, 64u, 128u, 256u}) {
    UINT16 mips = 1;
    for (UINT s = size; s > 1; s >>= 1)
      ++mips;
    cases.push_back({size, size, mips, DXGI_FORMAT_R8G8B8A8_UNORM,
                     D3D12_RESOURCE_FLAG_NONE});
  }
  return cases;
}

class Texture2DSizeMatrixSpec
    : public ::testing::TestWithParam<Texture2DSizeCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(Texture2DSizeMatrixSpec, CreatesCommittedTextureWithMatchingDesc) {
  const auto &test = GetParam();
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = test.width;
  desc.Height = test.height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = test.mips;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Flags = test.flags;
  D3D12_CLEAR_VALUE clear = {};
  clear.Format = test.format;
  const bool needs_clear =
      (test.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
  ComPtr<ID3D12Resource> resource;
  const HRESULT hr = context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
      needs_clear ? &clear : nullptr, IID_PPV_ARGS(resource.put()));
  ASSERT_EQ(hr, S_OK) << "w=" << test.width << " h=" << test.height
                      << " mips=" << test.mips
                      << " fmt=" << static_cast<UINT>(test.format)
                      << " flags=" << static_cast<UINT>(test.flags);
  ASSERT_TRUE(resource);
  const auto actual = resource->GetDesc();
  EXPECT_EQ(actual.Dimension, D3D12_RESOURCE_DIMENSION_TEXTURE2D);
  EXPECT_EQ(actual.Width, test.width);
  EXPECT_EQ(actual.Height, test.height);
  EXPECT_EQ(actual.MipLevels, test.mips);
  EXPECT_EQ(actual.Format, test.format);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string Texture2DSizeName(
    const ::testing::TestParamInfo<Texture2DSizeCase> &info) {
  return "W" + std::to_string(info.param.width) + "H" +
         std::to_string(info.param.height) + "M" +
         std::to_string(info.param.mips) + "F" +
         std::to_string(static_cast<UINT>(info.param.format)) + "G" +
         std::to_string(static_cast<UINT>(info.param.flags)) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(SizeMatrix, Texture2DSizeMatrixSpec,
                         ::testing::ValuesIn(BuildTexture2DSizeCases()),
                         Texture2DSizeName);

} // namespace
