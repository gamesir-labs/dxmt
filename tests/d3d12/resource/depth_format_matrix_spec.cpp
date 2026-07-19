#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <utility>
#include <vector>

// Public D3D12 depth-stencil resource/format creation matrix.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct DepthFormatCase {
  UINT width;
  UINT height;
  DXGI_FORMAT format;
  DXGI_FORMAT clear_format;
};

std::vector<DepthFormatCase> BuildDepthFormatCases() {
  std::vector<DepthFormatCase> cases;
  const DXGI_FORMAT formats[] = {
      DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D16_UNORM,
      DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
  };
  const std::pair<UINT, UINT> extents[] = {
      {1, 1}, {2, 1024}, {1024, 2}, {31, 33}, {512, 512}, {1024, 1024}};
  for (const auto format : formats) {
    for (const auto [width, height] : extents)
      cases.push_back({width, height, format, format});
  }
  return cases;
}

class DepthFormatMatrixSpec
    : public ::testing::TestWithParam<DepthFormatCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(DepthFormatMatrixSpec, CreatesDepthTextureAndDsv) {
  const auto &test = GetParam();
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = test.width;
  desc.Height = test.height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE clear = {};
  clear.Format = test.clear_format;
  clear.DepthStencil.Depth = 1.0f;
  clear.DepthStencil.Stencil = 0;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                IID_PPV_ARGS(resource.put())),
            S_OK)
      << "fmt=" << static_cast<UINT>(test.format) << " w=" << test.width
      << " h=" << test.height;
  ASSERT_TRUE(resource);
  auto heap_dsv = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(heap_dsv);
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
  dsv.Format = test.format;
  dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  context_.device()->CreateDepthStencilView(
      resource.get(), &dsv, heap_dsv->GetCPUDescriptorHandleForHeapStart());
  const FLOAT color[4] = {0, 0, 0, 0};
  // ClearDepthStencilView exercises the DSV handle without a color RT.
  context_.list()->ClearDepthStencilView(
      heap_dsv->GetCPUDescriptorHandleForHeapStart(),
      D3D12_CLEAR_FLAG_DEPTH |
          ((test.format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
            test.format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
               ? D3D12_CLEAR_FLAG_STENCIL
               : static_cast<D3D12_CLEAR_FLAGS>(0)),
      0.25f, 1, 0, nullptr);
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  (void)color;
}

std::string DepthFormatName(
    const ::testing::TestParamInfo<DepthFormatCase> &info) {
  return "F" + std::to_string(static_cast<UINT>(info.param.format)) + "W" +
         std::to_string(info.param.width) + "H" +
         std::to_string(info.param.height) + "N" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(FormatMatrix, DepthFormatMatrixSpec,
                         ::testing::ValuesIn(BuildDepthFormatCases()),
                         DepthFormatName);

} // namespace
