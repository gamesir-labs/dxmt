#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include "d3d12_test_context.hpp"

#include <array>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class TestWindow {
public:
  TestWindow() {
    handle_ = CreateWindowExW(0, L"STATIC", L"DXMT D3D12 swapchain test",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              64, 64, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (handle_) {
      ShowWindow(handle_, SW_SHOWNA);
      UpdateWindow(handle_);
    }
  }

  ~TestWindow() {
    if (handle_)
      DestroyWindow(handle_);
  }

  TestWindow(const TestWindow &) = delete;
  TestWindow &operator=(const TestWindow &) = delete;

  HWND get() const { return handle_; }

private:
  HWND handle_ = nullptr;
};

class D3D12SwapChainSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(window_.get());
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(CreateDXGIFactory2(0, __uuidof(IDXGIFactory4),
                                 reinterpret_cast<void **>(factory_.put())),
              S_OK);
  }

  ComPtr<IDXGISwapChain3> CreateSwapChain(UINT buffer_count, UINT flags = 0) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = buffer_count;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = flags;

    ComPtr<IDXGISwapChain1> base;
    EXPECT_EQ(factory_->CreateSwapChainForHwnd(context_.queue(), window_.get(),
                                               &desc, nullptr, nullptr,
                                               base.put()),
              S_OK);
    ComPtr<IDXGISwapChain3> swapchain;
    if (base) {
      EXPECT_EQ(
          base->QueryInterface(__uuidof(IDXGISwapChain3),
                               reinterpret_cast<void **>(swapchain.put())),
          S_OK);
    }
    return swapchain;
  }

  static constexpr UINT kWidth = 32;
  static constexpr UINT kHeight = 24;
  TestWindow window_;
  D3D12TestContext context_;
  ComPtr<IDXGIFactory4> factory_;
};

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec, CreatesRequestedBackBuffers) {
  constexpr UINT kBufferCount = 3;
  auto swapchain = CreateSwapChain(kBufferCount);
  ASSERT_TRUE(swapchain);

  DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
  ASSERT_EQ(swapchain->GetDesc1(&swapchain_desc), S_OK);
  EXPECT_EQ(swapchain_desc.Width, kWidth);
  EXPECT_EQ(swapchain_desc.Height, kHeight);
  EXPECT_EQ(swapchain_desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
  EXPECT_EQ(swapchain_desc.BufferCount, kBufferCount);
  EXPECT_EQ(swapchain_desc.SwapEffect, DXGI_SWAP_EFFECT_FLIP_DISCARD);

  std::array<ComPtr<ID3D12Resource>, kBufferCount> buffers;
  for (UINT index = 0; index < buffers.size(); ++index) {
    ASSERT_EQ(
        swapchain->GetBuffer(index, __uuidof(ID3D12Resource),
                             reinterpret_cast<void **>(buffers[index].put())),
        S_OK);
    const auto resource_desc = buffers[index]->GetDesc();
    EXPECT_EQ(resource_desc.Width, kWidth);
    EXPECT_EQ(resource_desc.Height, kHeight);
    EXPECT_EQ(resource_desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_NE(resource_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
              0u);
    for (UINT previous = 0; previous < index; ++previous)
      EXPECT_NE(buffers[index].get(), buffers[previous].get());
  }
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), 0u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec, PresentAdvancesIndexAndStatistics) {
  constexpr UINT kBufferCount = 3;
  auto swapchain = CreateSwapChain(kBufferCount);
  ASSERT_TRUE(swapchain);

  EXPECT_EQ(swapchain->Present(0, DXGI_PRESENT_TEST), S_OK);
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), 0u);
  UINT present_count = UINT_MAX;
  ASSERT_EQ(swapchain->GetLastPresentCount(&present_count), S_OK);
  EXPECT_EQ(present_count, 0u);

  ASSERT_EQ(swapchain->Present(0, 0), S_OK);
  EXPECT_EQ(context_.SignalAndWait(), S_OK);
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), 1u);
  ASSERT_EQ(swapchain->GetLastPresentCount(&present_count), S_OK);
  EXPECT_EQ(present_count, 1u);
  DXGI_FRAME_STATISTICS statistics = {};
  ASSERT_EQ(swapchain->GetFrameStatistics(&statistics), S_OK);
  EXPECT_EQ(statistics.PresentCount, 1u);
  EXPECT_EQ(statistics.PresentRefreshCount, 1u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   ResizeRequiresReleasedBuffersAndRecreatesThem) {
  constexpr UINT kResizedBufferCount = 3;
  constexpr UINT kResizedWidth = 48;
  constexpr UINT kResizedHeight = 36;
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);

  ComPtr<ID3D12Resource> buffer;
  ASSERT_EQ(swapchain->GetBuffer(0, __uuidof(ID3D12Resource),
                                 reinterpret_cast<void **>(buffer.put())),
            S_OK);
  EXPECT_EQ(swapchain->ResizeBuffers(kResizedBufferCount, kResizedWidth,
                                     kResizedHeight, DXGI_FORMAT_UNKNOWN, 0),
            DXGI_ERROR_INVALID_CALL);

  buffer.reset();
  ASSERT_EQ(swapchain->ResizeBuffers(kResizedBufferCount, kResizedWidth,
                                     kResizedHeight, DXGI_FORMAT_UNKNOWN, 0),
            S_OK);
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  ASSERT_EQ(swapchain->GetDesc1(&desc), S_OK);
  EXPECT_EQ(desc.Width, kResizedWidth);
  EXPECT_EQ(desc.Height, kResizedHeight);
  EXPECT_EQ(desc.BufferCount, kResizedBufferCount);
  EXPECT_EQ(desc.Format, DXGI_FORMAT_R8G8B8A8_UNORM);

  ASSERT_EQ(swapchain->GetBuffer(kResizedBufferCount - 1,
                                 __uuidof(ID3D12Resource),
                                 reinterpret_cast<void **>(buffer.put())),
            S_OK);
  const auto resource_desc = buffer->GetDesc();
  EXPECT_EQ(resource_desc.Width, kResizedWidth);
  EXPECT_EQ(resource_desc.Height, kResizedHeight);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   Present1ValidatesSyncFlagsAndDirtyParameters) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);

  EXPECT_EQ(swapchain->Present1(5, 0, nullptr), DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(swapchain->Present1(0, DXGI_PRESENT_DO_NOT_WAIT, nullptr),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(swapchain->Present1(0, DXGI_PRESENT_ALLOW_TEARING, nullptr),
            DXGI_ERROR_INVALID_CALL);

  RECT dirty_rect = {0, 0, 4, 4};
  DXGI_PRESENT_PARAMETERS parameters = {};
  parameters.DirtyRectsCount = 1;
  parameters.pDirtyRects = &dirty_rect;
  EXPECT_EQ(swapchain->Present1(0, 0, &parameters), DXGI_ERROR_UNSUPPORTED);

  POINT scroll_offset = {1, 1};
  parameters = {};
  parameters.pScrollRect = &dirty_rect;
  parameters.pScrollOffset = &scroll_offset;
  EXPECT_EQ(swapchain->Present1(0, 0, &parameters), DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), 0u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   TearingCapabilityMatchesCreationAndPresentRules) {
  ComPtr<IDXGIFactory5> factory5;
  ASSERT_EQ(factory_->QueryInterface(IID_PPV_ARGS(factory5.put())), S_OK);
  BOOL allow_tearing = FALSE;
  ASSERT_EQ(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                          &allow_tearing,
                                          sizeof(allow_tearing)),
            S_OK);
  ASSERT_TRUE(allow_tearing);

  auto swapchain = CreateSwapChain(2, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
  ASSERT_TRUE(swapchain);
  EXPECT_EQ(swapchain->Present1(1, DXGI_PRESENT_ALLOW_TEARING, nullptr),
            DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(swapchain->Present1(
                0, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_TEST, nullptr),
            S_OK);
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), 0u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   FrameLatencyWaitableObjectTracksConfiguredLatency) {
  auto swapchain =
      CreateSwapChain(3, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
  ASSERT_TRUE(swapchain);

  EXPECT_EQ(swapchain->SetMaximumFrameLatency(0), E_INVALIDARG);
  EXPECT_EQ(swapchain->SetMaximumFrameLatency(DXGI_MAX_SWAP_CHAIN_BUFFERS + 1),
            E_INVALIDARG);
  ASSERT_EQ(swapchain->SetMaximumFrameLatency(1), S_OK);
  UINT latency = 0;
  ASSERT_EQ(swapchain->GetMaximumFrameLatency(&latency), S_OK);
  EXPECT_EQ(latency, 1u);

  HANDLE waitable = swapchain->GetFrameLatencyWaitableObject();
  ASSERT_NE(waitable, nullptr);
  EXPECT_EQ(WaitForSingleObject(waitable, 0), WAIT_OBJECT_0);
  CloseHandle(waitable);

  swapchain.reset();
  auto ordinary = CreateSwapChain(2);
  ASSERT_TRUE(ordinary);
  EXPECT_EQ(ordinary->GetFrameLatencyWaitableObject(), nullptr);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   ResizeBuffers1ValidatesArraysAndRecreatesBuffers) {
  constexpr UINT kBufferCount = 3;
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);

  EXPECT_EQ(swapchain->ResizeBuffers1(kBufferCount, 40, 30, DXGI_FORMAT_UNKNOWN,
                                      0, nullptr, nullptr),
            DXGI_ERROR_INVALID_CALL);

  std::array<UINT, kBufferCount> invalid_masks = {1, 2, 1};
  std::array<IUnknown *, kBufferCount> queues = {
      context_.queue(), context_.queue(), context_.queue()};
  EXPECT_EQ(swapchain->ResizeBuffers1(kBufferCount, 40, 30, DXGI_FORMAT_UNKNOWN,
                                      0, invalid_masks.data(), queues.data()),
            DXGI_ERROR_INVALID_CALL);

  std::array<UINT, kBufferCount> masks = {1, 1, 1};
  std::array<IUnknown *, kBufferCount> invalid_queues = {
      context_.queue(), context_.device(), context_.queue()};
  EXPECT_EQ(swapchain->ResizeBuffers1(kBufferCount, 40, 30, DXGI_FORMAT_UNKNOWN,
                                      0, masks.data(), invalid_queues.data()),
            DXGI_ERROR_INVALID_CALL);

  ASSERT_EQ(swapchain->ResizeBuffers1(kBufferCount, 40, 30, DXGI_FORMAT_UNKNOWN,
                                      0, masks.data(), queues.data()),
            S_OK);
  DXGI_SWAP_CHAIN_DESC1 desc = {};
  ASSERT_EQ(swapchain->GetDesc1(&desc), S_OK);
  EXPECT_EQ(desc.BufferCount, kBufferCount);
  EXPECT_EQ(desc.Width, 40u);
  EXPECT_EQ(desc.Height, 30u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec,
                   ColorSpaceAndHdrMetadataMatchReportedSupport) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);
  ComPtr<IDXGISwapChain4> swapchain4;
  ASSERT_EQ(swapchain->QueryInterface(IID_PPV_ARGS(swapchain4.put())), S_OK);

  UINT support = 0;
  ASSERT_EQ(swapchain4->CheckColorSpaceSupport(
                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &support),
            S_OK);
  ASSERT_NE(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT, 0u);
  EXPECT_EQ(swapchain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),
            S_OK);

  support = UINT_MAX;
  ASSERT_EQ(swapchain4->CheckColorSpaceSupport(
                DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support),
            S_OK);
  EXPECT_EQ(support, 0u);
  EXPECT_EQ(
      swapchain4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020),
      DXGI_ERROR_UNSUPPORTED);

  EXPECT_EQ(swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr),
            S_OK);
  DXGI_HDR_METADATA_HDR10 metadata = {};
  EXPECT_EQ(swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,
                                       sizeof(metadata), &metadata),
            DXGI_ERROR_UNSUPPORTED);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainSpec, PresentIndexWrapsAcrossBufferCount) {
  constexpr UINT kBufferCount = 3;
  auto swapchain = CreateSwapChain(kBufferCount);
  ASSERT_TRUE(swapchain);
  for (UINT present = 1; present <= kBufferCount * 2; ++present) {
    ASSERT_EQ(swapchain->Present(0, 0), S_OK);
    ASSERT_EQ(context_.SignalAndWait(), S_OK);
    EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), present % kBufferCount);
  }
}

} // namespace
