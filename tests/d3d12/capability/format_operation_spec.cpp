#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class FeatureCoherenceSpec : public ::testing::Test {
protected:
  struct UavCase {
    DXGI_FORMAT format;
    bool float_clear;
  };

  struct DepthCase {
    DXGI_FORMAT format;
    bool has_stencil;
  };

  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support(DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CheckFeatureSupport(
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))));
    return support;
  }

  ComPtr<ID3D12Resource> CreateTexture(DXGI_FORMAT format,
                                       D3D12_RESOURCE_FLAGS flags,
                                       D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 4;
    desc.Height = 4;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> texture;
    EXPECT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(texture.put()))));
    return texture;
  }

  HRESULT InitializeTexture(ID3D12Resource *texture) {
    const auto desc = texture->GetDesc();
    UINT rows = 0;
    UINT64 row_size = 0;
    context_.device()->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, &rows,
                                             &row_size, nullptr);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(rows) * row_size,
                                   0xff);
    return context_.UploadTextureAndReset(texture, data.data(), row_size,
                                          data.size());
  }

  void ExpectTextureZero(ID3D12Resource *texture) {
    const auto desc = texture->GetDesc();
    UINT rows = 0;
    UINT64 row_size = 0;
    context_.device()->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, &rows,
                                             &row_size, nullptr);
    TextureReadback readback;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture, &readback)));
    for (UINT row = 0; row < rows; ++row) {
      const auto *begin = readback.data.data() + row * readback.row_pitch;
      EXPECT_TRUE(std::all_of(begin, begin + row_size,
                              [](std::uint8_t value) { return value == 0; }))
          << "row=" << row;
    }
    ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  }

  void ExecuteRenderTarget(DXGI_FORMAT format) {
    auto texture =
        CreateTexture(format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                      D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(SUCCEEDED(InitializeTexture(texture.get())));
    D3D12_RENDER_TARGET_VIEW_DESC view = {};
    view.Format = format;
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(texture.get(), &view, handle);
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    constexpr FLOAT clear[4] = {};
    context_.list()->ClearRenderTargetView(handle, clear, 0, nullptr);
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    ExpectTextureZero(texture.get());
  }

  void ExecuteUav(const UavCase &operation) {
    auto texture = CreateTexture(operation.format,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
    auto heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(heap);
    ASSERT_TRUE(SUCCEEDED(InitializeTexture(texture.get())));
    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
    view.Format = operation.format;
    view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    context_.device()->CreateUnorderedAccessView(texture.get(), nullptr, &view,
                                                 cpu);
    ID3D12DescriptorHeap *heaps[] = {heap.get()};
    context_.list()->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (operation.float_clear) {
      constexpr FLOAT clear[4] = {};
      context_.list()->ClearUnorderedAccessViewFloat(gpu, cpu, texture.get(),
                                                     clear, 0, nullptr);
    } else {
      constexpr UINT clear[4] = {};
      context_.list()->ClearUnorderedAccessViewUint(gpu, cpu, texture.get(),
                                                    clear, 0, nullptr);
    }
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    ExpectTextureZero(texture.get());
  }

  void ExecuteDepthStencil(const DepthCase &operation) {
    auto texture =
        CreateTexture(operation.format, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                      D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto heap =
        context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(heap);
    D3D12_DEPTH_STENCIL_VIEW_DESC view = {};
    view.Format = operation.format;
    view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateDepthStencilView(texture.get(), &view, handle);
    const auto flags = static_cast<D3D12_CLEAR_FLAGS>(
        D3D12_CLEAR_FLAG_DEPTH |
        (operation.has_stencil ? D3D12_CLEAR_FLAG_STENCIL : 0));
    constexpr FLOAT expected_depth = 0.375f;
    context_.list()->ClearDepthStencilView(handle, flags, expected_depth, 0xa5,
                                           0, nullptr);
    D3D12TestContext::Transition(context_.list(), texture.get(),
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        if (operation.format == DXGI_FORMAT_D16_UNORM) {
          std::uint16_t actual = 0;
          std::memcpy(&actual,
                      readback.data.data() + y * readback.row_pitch +
                          x * sizeof(actual),
                      sizeof(actual));
          EXPECT_NEAR(actual, 24576, 1) << "pixel (" << x << ", " << y << ")";
        } else {
          FLOAT actual = 0.0f;
          std::memcpy(&actual,
                      readback.data.data() + y * readback.row_pitch +
                          x * sizeof(actual),
                      sizeof(actual));
          EXPECT_NEAR(actual, expected_depth, 1.0e-6f)
              << "pixel (" << x << ", " << y << ")";
        }
      }
    }
    ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
  }

  D3D12TestContext context_;
};

TEST_F(FeatureCoherenceSpec, EveryAdvertisedFormatOperationExecutes) {
  constexpr std::array render_targets = {
      DXGI_FORMAT_R8_UNORM,        DXGI_FORMAT_R8_UINT,
      DXGI_FORMAT_R8G8_UNORM,      DXGI_FORMAT_R16_UNORM,
      DXGI_FORMAT_R16_FLOAT,       DXGI_FORMAT_R16G16_FLOAT,
      DXGI_FORMAT_R8G8B8A8_UNORM,  DXGI_FORMAT_R8G8B8A8_UINT,
      DXGI_FORMAT_B8G8R8A8_UNORM,  DXGI_FORMAT_R10G10B10A2_UNORM,
      DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
      DXGI_FORMAT_R32_FLOAT,       DXGI_FORMAT_R32_UINT,
      DXGI_FORMAT_R32G32_FLOAT,    DXGI_FORMAT_R32G32B32A32_FLOAT,
  };
  constexpr std::array uavs = {
      UavCase{DXGI_FORMAT_R8_UNORM, true},
      UavCase{DXGI_FORMAT_R8_UINT, false},
      UavCase{DXGI_FORMAT_R8G8_UNORM, true},
      UavCase{DXGI_FORMAT_R16_UNORM, true},
      UavCase{DXGI_FORMAT_R16_FLOAT, true},
      UavCase{DXGI_FORMAT_R16G16_FLOAT, true},
      UavCase{DXGI_FORMAT_R8G8B8A8_UNORM, true},
      UavCase{DXGI_FORMAT_R8G8B8A8_UINT, false},
      UavCase{DXGI_FORMAT_R16G16B16A16_FLOAT, true},
      UavCase{DXGI_FORMAT_R32_FLOAT, true},
      UavCase{DXGI_FORMAT_R32_UINT, false},
      UavCase{DXGI_FORMAT_R32G32_FLOAT, true},
      UavCase{DXGI_FORMAT_R32G32B32A32_FLOAT, true},
  };
  constexpr std::array depth_stencils = {
      DepthCase{DXGI_FORMAT_D16_UNORM, false},
      DepthCase{DXGI_FORMAT_D32_FLOAT, false},
      DepthCase{DXGI_FORMAT_D24_UNORM_S8_UINT, true},
      DepthCase{DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true},
  };
  UINT executed = 0;

  for (const auto format : render_targets) {
    SCOPED_TRACE(::testing::Message()
                 << "RTV format=" << static_cast<UINT>(format));
    if (!(Support(format).Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
      continue;
    ASSERT_NO_FATAL_FAILURE(ExecuteRenderTarget(format));
    ++executed;
  }
  for (const auto &operation : uavs) {
    SCOPED_TRACE(::testing::Message()
                 << "UAV format=" << static_cast<UINT>(operation.format));
    if (!(Support(operation.format).Support1 &
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
      continue;
    ASSERT_NO_FATAL_FAILURE(ExecuteUav(operation));
    ++executed;
  }
  for (const auto &operation : depth_stencils) {
    SCOPED_TRACE(::testing::Message()
                 << "DSV format=" << static_cast<UINT>(operation.format));
    if (!(Support(operation.format).Support1 &
          D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
      continue;
    ASSERT_NO_FATAL_FAILURE(ExecuteDepthStencil(operation));
    ++executed;
  }

  EXPECT_GT(executed, 0u);
}

TEST_F(FeatureCoherenceSpec,
       EveryEnumeratedFormatReportsInternallyCoherentCapabilities) {
  UINT queried = 0;
  for (UINT value = DXGI_FORMAT_R32G32B32A32_TYPELESS;
       value <= DXGI_FORMAT_V408; ++value) {
    const auto format = static_cast<DXGI_FORMAT>(value);
    SCOPED_TRACE(::testing::Message() << "format=" << value);
    const auto support = Support(format);
    const auto texture_dimensions =
        D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
        D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE;
    if (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) {
      EXPECT_NE(support.Support1 & texture_dimensions, 0u);
    }
    if (support.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) {
      EXPECT_NE(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET, 0u);
    }
    if (support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE) {
      EXPECT_NE(support.Support1 &
                    D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET,
                0u);
    }
    if (support.Support2 & (D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
                            D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
      EXPECT_NE(support.Support1 &
                    D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
                0u);
    }
    ++queried;
  }
  EXPECT_EQ(queried, static_cast<UINT>(DXGI_FORMAT_V408));
}

TEST_F(FeatureCoherenceSpec,
       EveryAdvertisedTexture2DFormatCreatesAndHasCopyableFootprint) {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  UINT advertised = 0;
  UINT unsupported = 0;
  for (UINT value = DXGI_FORMAT_R32G32B32A32_TYPELESS;
       value <= DXGI_FORMAT_V408; ++value) {
    const auto format = static_cast<DXGI_FORMAT>(value);
    SCOPED_TRACE(::testing::Message() << "format=" << value);
    const auto support = Support(format);
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)) {
      ++unsupported;
      continue;
    }
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 8;
    desc.Height = 8;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> texture;
    const HRESULT result = context_.device()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(texture.put()));

    ASSERT_EQ(result, S_OK);
    ASSERT_TRUE(texture);
    D3D12_FEATURE_DATA_FORMAT_INFO info = {};
    info.Format = format;
    ASSERT_EQ(context_.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                                                     &info, sizeof(info)),
              S_OK);
    ASSERT_GT(info.PlaneCount, 0u);
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(info.PlaneCount);
    std::vector<UINT> rows(info.PlaneCount);
    std::vector<UINT64> row_sizes(info.PlaneCount);
    UINT64 total_size = 0;
    context_.device()->GetCopyableFootprints(&desc, 0, info.PlaneCount, 0,
                                             footprints.data(), rows.data(),
                                             row_sizes.data(), &total_size);
    EXPECT_NE(total_size, 0u);
    EXPECT_NE(total_size, UINT64_MAX);
    for (UINT plane = 0; plane < info.PlaneCount; ++plane) {
      EXPECT_NE(footprints[plane].Offset, UINT64_MAX);
      EXPECT_GT(footprints[plane].Footprint.Width, 0u);
      EXPECT_GT(footprints[plane].Footprint.Height, 0u);
      EXPECT_GT(rows[plane], 0u);
      EXPECT_GT(row_sizes[plane], 0u);
    }
    ++advertised;
  }
  EXPECT_GT(advertised, 0u);
  EXPECT_GT(unsupported, 0u);
}

} // namespace
