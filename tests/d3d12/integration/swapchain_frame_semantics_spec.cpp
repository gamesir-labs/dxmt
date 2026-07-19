#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class TestWindow {
public:
  TestWindow() {
    handle_ = CreateWindowExW(
        0, L"STATIC", L"DXMT D3D12 swapchain frame semantics test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr,
        nullptr, GetModuleHandleW(nullptr), nullptr);
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

struct CommandRecording {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
};

class D3D12SwapChainFrameSpec : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    window_ = std::make_unique<TestWindow>();
    ASSERT_TRUE(window_->get());
    ASSERT_EQ(CreateDXGIFactory2(0, __uuidof(IDXGIFactory4),
                                 reinterpret_cast<void **>(factory_.put())),
              S_OK);
  }

  static void TearDownTestSuite() {
    factory_.reset();
    window_.reset();
  }

  void SetUp() override {
    ASSERT_EQ(context_.InitializeSharedDevice("d3d12-swapchain"), S_OK);
  }

  DXGI_SWAP_CHAIN_DESC1 ValidDesc(
      UINT buffer_count,
      DXGI_SWAP_EFFECT swap_effect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) const {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = buffer_count;
    desc.SwapEffect = swap_effect;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    return desc;
  }

  ComPtr<IDXGISwapChain3>
  CreateSwapChain(UINT buffer_count,
                  DXGI_SWAP_EFFECT swap_effect =
                      DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
    const auto desc = ValidDesc(buffer_count, swap_effect);
    ComPtr<IDXGISwapChain1> base;
    EXPECT_EQ(factory_->CreateSwapChainForHwnd(
                  context_.queue(), window_->get(), &desc, nullptr, nullptr,
                  base.put()),
              S_OK);
    ComPtr<IDXGISwapChain3> swapchain;
    if (base) {
      EXPECT_EQ(base->QueryInterface(
                    __uuidof(IDXGISwapChain3),
                    reinterpret_cast<void **>(swapchain.put())),
                S_OK);
    }
    return swapchain;
  }

  ComPtr<IDXGISwapChain3> CreateCompositionSwapChain(UINT buffer_count) {
    const auto desc = ValidDesc(buffer_count);
    ComPtr<IDXGISwapChain1> base;
    EXPECT_EQ(factory_->CreateSwapChainForComposition(
                  context_.queue(), &desc, nullptr, base.put()),
              S_OK);
    ComPtr<IDXGISwapChain3> swapchain;
    if (base) {
      EXPECT_EQ(base->QueryInterface(
                    __uuidof(IDXGISwapChain3),
                    reinterpret_cast<void **>(swapchain.put())),
                S_OK);
    }
    return swapchain;
  }

  ComPtr<ID3D12Resource> GetBuffer(IDXGISwapChain3 *swapchain, UINT index) {
    ComPtr<ID3D12Resource> buffer;
    EXPECT_EQ(swapchain->GetBuffer(
                  index, __uuidof(ID3D12Resource),
                  reinterpret_cast<void **>(buffer.put())),
              S_OK);
    return buffer;
  }

  CommandRecording CreateRecording() {
    CommandRecording recording;
    EXPECT_EQ(context_.device()->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT,
                  __uuidof(ID3D12CommandAllocator),
                  reinterpret_cast<void **>(recording.allocator.put())),
              S_OK);
    if (!recording.allocator)
      return recording;
    EXPECT_EQ(context_.device()->CreateCommandList(
                  0, D3D12_COMMAND_LIST_TYPE_DIRECT, recording.allocator.get(),
                  nullptr, __uuidof(ID3D12GraphicsCommandList),
                  reinterpret_cast<void **>(recording.list.put())),
              S_OK);
    return recording;
  }

  void RecordClear(ID3D12GraphicsCommandList *list, ID3D12Resource *buffer,
                   D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                   const std::array<float, 4> &color) {
    context_.device()->CreateRenderTargetView(buffer, nullptr, rtv);
    D3D12TestContext::Transition(list, buffer, D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ClearRenderTargetView(rtv, color.data(), 0, nullptr);
    D3D12TestContext::Transition(list, buffer,
                                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 D3D12_RESOURCE_STATE_PRESENT);
  }

  void ExpectSolidColor(ID3D12Resource *buffer, std::uint32_t expected) {
    D3D12TestContext::Transition(context_.list(), buffer,
                                 D3D12_RESOURCE_STATE_PRESENT,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    ASSERT_EQ(context_.ReadbackTexture(buffer, &readback), S_OK);
    ASSERT_EQ(readback.width, kWidth);
    ASSERT_EQ(readback.height, kHeight);

    for (UINT y = 0; y < readback.height; ++y) {
      for (UINT x = 0; x < readback.width; ++x) {
        std::uint32_t actual = 0;
        std::memcpy(&actual,
                    readback.data.data() + y * readback.row_pitch +
                        x * sizeof(actual),
                    sizeof(actual));
        if (!ColorsMatch(actual, expected, 1)) {
          ADD_FAILURE() << "pixel (" << x << ", " << y << ") was 0x"
                        << std::hex << actual << ", expected 0x" << expected;
          return;
        }
      }
    }
  }

  static constexpr UINT kWidth = 32;
  static constexpr UINT kHeight = 24;
  inline static std::unique_ptr<TestWindow> window_;
  inline static ComPtr<IDXGIFactory4> factory_;
  D3D12TestContext context_;
};

DXMT_GROUP_SERIAL_TESTS("D3D12SwapChainFrameSpec.*", "d3d12-swapchain");
DXMT_SERIAL_TEST_DOMAIN("D3D12SwapChainFrameSpec.*", "swapchain");

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   PresentWaitsForPriorQueueRendering) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);
  auto buffer = GetBuffer(swapchain.get(), 0);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(rtv_heap);

  constexpr std::array<float, 4> kColor = {0.125f, 0.25f, 0.5f, 1.0f};
  RecordClear(context_.list(), buffer.get(),
              context_.CpuDescriptorHandle(rtv_heap.get(), 0), kColor);
  ASSERT_EQ(context_.list()->Close(), S_OK);
  ID3D12CommandList *lists[] = {context_.list()};
  context_.queue()->ExecuteCommandLists(1, lists);

  ASSERT_EQ(swapchain->Present(0, 0), S_OK);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  ASSERT_EQ(context_.ResetCommandList(), S_OK);
  ExpectSolidColor(buffer.get(), 0xff804020u);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   MultipleFramesInFlightPreservePerBufferContents) {
  constexpr UINT kBufferCount = 3;
  constexpr std::array<std::array<float, 4>, kBufferCount> kColors = {{
      {1.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
  }};
  constexpr std::array<std::uint32_t, kBufferCount> kExpected = {
      0xff0000ffu, 0xff00ff00u, 0xffff0000u};

  auto swapchain = CreateSwapChain(kBufferCount);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kBufferCount, false);
  ASSERT_TRUE(swapchain);
  ASSERT_TRUE(rtv_heap);

  std::array<ComPtr<ID3D12Resource>, kBufferCount> buffers;
  std::array<CommandRecording, kBufferCount> recordings;
  for (UINT index = 0; index < kBufferCount; ++index) {
    ASSERT_EQ(swapchain->GetCurrentBackBufferIndex(), index);
    buffers[index] = GetBuffer(swapchain.get(), index);
    recordings[index] = CreateRecording();
    ASSERT_TRUE(buffers[index]);
    ASSERT_TRUE(recordings[index].list);
    RecordClear(recordings[index].list.get(), buffers[index].get(),
                context_.CpuDescriptorHandle(rtv_heap.get(), index),
                kColors[index]);
    ASSERT_EQ(recordings[index].list->Close(), S_OK);
    ID3D12CommandList *lists[] = {recordings[index].list.get()};
    context_.queue()->ExecuteCommandLists(1, lists);
    ASSERT_EQ(swapchain->Present(0, 0), S_OK);
  }

  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  for (UINT index = 0; index < kBufferCount; ++index) {
    if (index) {
      ASSERT_EQ(context_.ResetCommandList(), S_OK);
    }
    SCOPED_TRACE(::testing::Message() << "back buffer " << index);
    ExpectSolidColor(buffers[index].get(), kExpected[index]);
  }
}

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   ResizeFailureWithOutstandingReferencesIsAtomic) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);
  auto original_buffer = GetBuffer(swapchain.get(), 0);
  ASSERT_TRUE(original_buffer);

  DXGI_SWAP_CHAIN_DESC1 before = {};
  ASSERT_EQ(swapchain->GetDesc1(&before), S_OK);
  const UINT before_index = swapchain->GetCurrentBackBufferIndex();
  ASSERT_EQ(swapchain->ResizeBuffers(3, 48, 36, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     0),
            DXGI_ERROR_INVALID_CALL);

  DXGI_SWAP_CHAIN_DESC1 after = {};
  ASSERT_EQ(swapchain->GetDesc1(&after), S_OK);
  EXPECT_EQ(after.Width, before.Width);
  EXPECT_EQ(after.Height, before.Height);
  EXPECT_EQ(after.Format, before.Format);
  EXPECT_EQ(after.BufferCount, before.BufferCount);
  EXPECT_EQ(swapchain->GetCurrentBackBufferIndex(), before_index);

  auto same_buffer = GetBuffer(swapchain.get(), 0);
  ASSERT_TRUE(same_buffer);
  EXPECT_EQ(same_buffer.get(), original_buffer.get());
}

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   ResizeAfterGpuCompletionRecreatesAllBuffers) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);
  auto buffer = GetBuffer(swapchain.get(), 0);
  auto rtv_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  auto recording = CreateRecording();
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(rtv_heap);
  ASSERT_TRUE(recording.list);

  constexpr std::array<float, 4> kColor = {0.5f, 0.25f, 0.125f, 1.0f};
  RecordClear(recording.list.get(), buffer.get(),
              context_.CpuDescriptorHandle(rtv_heap.get(), 0), kColor);
  ASSERT_EQ(recording.list->Close(), S_OK);
  ID3D12CommandList *lists[] = {recording.list.get()};
  context_.queue()->ExecuteCommandLists(1, lists);
  ASSERT_EQ(context_.SignalAndWait(), S_OK);
  recording.list.reset();
  recording.allocator.reset();
  buffer.reset();
  rtv_heap.reset();

  constexpr UINT kResizedBufferCount = 3;
  constexpr UINT kResizedWidth = 48;
  constexpr UINT kResizedHeight = 36;
  ASSERT_EQ(swapchain->ResizeBuffers(kResizedBufferCount, kResizedWidth,
                                     kResizedHeight, DXGI_FORMAT_UNKNOWN, 0),
            S_OK);

  std::array<ComPtr<ID3D12Resource>, kResizedBufferCount> resized;
  for (UINT index = 0; index < kResizedBufferCount; ++index) {
    resized[index] = GetBuffer(swapchain.get(), index);
    ASSERT_TRUE(resized[index]);
    const auto desc = resized[index]->GetDesc();
    EXPECT_EQ(desc.Width, kResizedWidth);
    EXPECT_EQ(desc.Height, kResizedHeight);
    for (UINT previous = 0; previous < index; ++previous)
      EXPECT_NE(resized[index].get(), resized[previous].get());
  }
}

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   WindowStatePropertiesRoundTrip) {
  auto swapchain = CreateSwapChain(2);
  ASSERT_TRUE(swapchain);

  ASSERT_EQ(swapchain->SetSourceSize(20, 12), S_OK);
  UINT source_width = 0;
  UINT source_height = 0;
  ASSERT_EQ(swapchain->GetSourceSize(&source_width, &source_height), S_OK);
  EXPECT_EQ(source_width, 20u);
  EXPECT_EQ(source_height, 12u);

  const DXGI_RGBA background = {0.1f, 0.2f, 0.3f, 0.4f};
  ASSERT_EQ(swapchain->SetBackgroundColor(&background), S_OK);
  DXGI_RGBA actual_background = {};
  ASSERT_EQ(swapchain->GetBackgroundColor(&actual_background), S_OK);
  EXPECT_FLOAT_EQ(actual_background.r, background.r);
  EXPECT_FLOAT_EQ(actual_background.g, background.g);
  EXPECT_FLOAT_EQ(actual_background.b, background.b);
  EXPECT_FLOAT_EQ(actual_background.a, background.a);

  ASSERT_EQ(swapchain->SetRotation(DXGI_MODE_ROTATION_ROTATE90), S_OK);
  DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
  ASSERT_EQ(swapchain->GetRotation(&rotation), S_OK);
  EXPECT_EQ(rotation, DXGI_MODE_ROTATION_ROTATE90);

  HWND hwnd = nullptr;
  ASSERT_EQ(swapchain->GetHwnd(&hwnd), S_OK);
  EXPECT_EQ(hwnd, window_->get());
  ASSERT_EQ(swapchain->SetFullscreenState(TRUE, nullptr), S_OK);
  BOOL fullscreen = FALSE;
  ASSERT_EQ(swapchain->GetFullscreenState(&fullscreen, nullptr), S_OK);
  EXPECT_TRUE(fullscreen);
  ASSERT_EQ(swapchain->SetFullscreenState(FALSE, nullptr), S_OK);
  ASSERT_EQ(swapchain->GetFullscreenState(&fullscreen, nullptr), S_OK);
  EXPECT_FALSE(fullscreen);
}

DXMT_SERIAL_TEST_F(D3D12SwapChainFrameSpec,
                   CompositionMatrixTransformRoundTrips) {
  auto swapchain = CreateCompositionSwapChain(2);
  ASSERT_TRUE(swapchain);

  const DXGI_MATRIX_3X2_F matrix = {1.0f, 0.25f, -0.5f,
                                    1.0f, 3.0f,  4.0f};
  ASSERT_EQ(swapchain->SetMatrixTransform(&matrix), S_OK);
  DXGI_MATRIX_3X2_F actual = {};
  ASSERT_EQ(swapchain->GetMatrixTransform(&actual), S_OK);
  EXPECT_EQ(std::memcmp(&actual, &matrix, sizeof(matrix)), 0);
}


} // namespace
