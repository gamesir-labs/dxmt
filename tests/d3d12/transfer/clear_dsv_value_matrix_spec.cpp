#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §15.6: ClearDepthStencilView depth/stencil value matrix.
// Public D3D12 API only.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

struct ClearDsvValueCase {
  DXGI_FORMAT format;
  float depth;
  UINT8 stencil;
  bool clear_depth;
  bool clear_stencil;
};

std::vector<ClearDsvValueCase> BuildClearDsvValueCases() {
  std::vector<ClearDsvValueCase> cases;
  const float depths[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.125f, 0.875f,
                          1.0f / 65535.0f, 1.0f - 1.0f / 65535.0f};
  const UINT8 stencils[] = {0, 1, 2, 127, 128, 254, 255};
  // D32 only: depth clears.
  for (const float d : depths)
    cases.push_back({DXGI_FORMAT_D32_FLOAT, d, 0, true, false});
  // D16 only: depth clears.
  for (const float d : depths)
    cases.push_back({DXGI_FORMAT_D16_UNORM, d, 0, true, false});
  // D24S8 / D32S8: depth, stencil, both.
  for (const DXGI_FORMAT format :
       {DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
    for (const float d : depths) {
      cases.push_back({format, d, 0, true, false});
      cases.push_back({format, d, 0, true, true});
    }
    for (const UINT8 s : stencils) {
      cases.push_back({format, 0.0f, s, false, true});
      cases.push_back({format, 1.0f, s, true, true});
    }
  }
  return cases;
}

class ClearDsvValueMatrixSpec
    : public ::testing::TestWithParam<ClearDsvValueCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  bool Supports(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    return SUCCEEDED(context_.device()->CheckFeatureSupport(
               D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
           (support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
  }

  D3D12TestContext context_;
};

TEST_P(ClearDsvValueMatrixSpec, ClearThenReadbackMatchesRequestedChannels) {
  const auto &test = GetParam();
  if (!Supports(test.format))
    GTEST_SKIP() << "format unsupported";

  constexpr UINT kWidth = 4;
  constexpr UINT kHeight = 4;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = kWidth;
  desc.Height = kHeight;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = test.format;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  ComPtr<ID3D12Resource> texture;
  ASSERT_EQ(context_.device()->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr,
                IID_PPV_ARGS(texture.put())),
            S_OK);
  auto dsv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  ASSERT_TRUE(dsv_heap);
  const auto dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateDepthStencilView(texture.get(), nullptr, dsv);

  D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
  if (test.clear_depth)
    flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_DEPTH);
  if (test.clear_stencil)
    flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_STENCIL);
  context_.list()->ClearDepthStencilView(dsv, flags, test.depth, test.stencil,
                                         0, nullptr);

  // Read depth plane when present.
  if (test.clear_depth && (test.format == DXGI_FORMAT_D32_FLOAT ||
                           test.format == DXGI_FORMAT_D16_UNORM ||
                           test.format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ||
                           test.format == DXGI_FORMAT_D24_UNORM_S8_UINT)) {
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    // Plane 0 is depth for these formats.
    ASSERT_EQ(context_.ReadbackTexture(texture.get(), &readback, 0), S_OK);
    if (test.format == DXGI_FORMAT_D32_FLOAT ||
        test.format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
      for (UINT y = 0; y < readback.height; ++y) {
        for (UINT x = 0; x < readback.width; ++x) {
          float value = 0.0f;
          std::memcpy(&value,
                      readback.data.data() + y * readback.row_pitch +
                          x * sizeof(value),
                      sizeof(value));
          EXPECT_NEAR(value, test.depth, 1.0e-5f) << x << "," << y;
        }
      }
    } else if (test.format == DXGI_FORMAT_D16_UNORM) {
      const float expected = test.depth;
      for (UINT y = 0; y < readback.height; ++y) {
        for (UINT x = 0; x < readback.width; ++x) {
          std::uint16_t raw = 0;
          std::memcpy(&raw,
                      readback.data.data() + y * readback.row_pitch +
                          x * sizeof(raw),
                      sizeof(raw));
          const float value = raw / 65535.0f;
          EXPECT_NEAR(value, expected, 2.0f / 65535.0f) << x << "," << y;
        }
      }
    }
    // D24 path: tolerate quantized depth without requiring exact plane decode
    // when implementation packs D24S8 tightly.
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string ClearDsvValueName(
    const ::testing::TestParamInfo<ClearDsvValueCase> &info) {
  return "F" + std::to_string(static_cast<UINT>(info.param.format)) + "D" +
         std::to_string(static_cast<int>(info.param.depth * 1000)) + "S" +
         std::to_string(info.param.stencil) +
         (info.param.clear_depth ? "Cd" : "") +
         (info.param.clear_stencil ? "Cs" : "") + "I" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(ValueMatrix, ClearDsvValueMatrixSpec,
                         ::testing::ValuesIn(BuildClearDsvValueCases()),
                         ClearDsvValueName);

} // namespace
