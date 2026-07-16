#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

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

  ComPtr<IDXGISwapChain3> CreateSwapChain(UINT buffer_count) {
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

} // namespace
