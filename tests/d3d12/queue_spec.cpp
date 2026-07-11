#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ColorsMatch;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class LifetimeProbe final : public IUnknown {
public:
  explicit LifetimeProbe(std::shared_ptr<std::atomic_bool> destroyed)
      : destroyed_(std::move(destroyed)) {}

  ~LifetimeProbe() { destroyed_->store(true, std::memory_order_release); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG refs = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!refs)
      delete this;
    return refs;
  }

private:
  std::atomic_ulong ref_count_{1};
  std::shared_ptr<std::atomic_bool> destroyed_;
};

class D3D12QueueSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  D3D12TestContext context_;
};

TEST_F(D3D12QueueSpec, CompletesBufferCopyBeforeFenceSignal) {
  const std::array<std::uint32_t, 16> expected = {
      0x01020304, 0x11121314, 0x21222324, 0x31323334, 0x41424344, 0x51525354,
      0x61626364, 0x71727374, 0x81828384, 0x91929394, 0xa1a2a3a4, 0xb1b2b3b4,
      0xc1c2c3c4, 0xd1d2d3d4, 0xe1e2e3e4, 0xf1f2f3f4,
  };
  ComPtr<ID3D12Resource> upload = context_.CreateUploadBuffer(
      sizeof(expected), expected.data(), sizeof(expected));
  ComPtr<ID3D12Resource> destination = context_.CreateBuffer(
      sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(destination);

  context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(SUCCEEDED(
      context_.ReadbackBuffer(destination.get(), sizeof(expected), &actual)));
  ASSERT_EQ(actual.size(), sizeof(expected));
  EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
}

TEST_F(D3D12QueueSpec, CompletesFenceSignalsInValueOrder) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 3)));
  ASSERT_TRUE(SUCCEEDED(context_.queue()->Signal(fence.get(), 7)));
  ASSERT_TRUE(SUCCEEDED(context_.WaitForFence(fence.get(), 3)));
  EXPECT_GE(fence->GetCompletedValue(), 3ull);
  ASSERT_TRUE(SUCCEEDED(context_.WaitForFence(fence.get(), 7)));
  EXPECT_GE(fence->GetCompletedValue(), 7ull);
}

TEST_F(D3D12QueueSpec, RejectsClosingAlreadyClosedCommandList) {
  ASSERT_TRUE(SUCCEEDED(context_.list()->Close()));
  EXPECT_EQ(context_.list()->Close(), E_FAIL);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));
}

TEST_F(D3D12QueueSpec, CopiesTextureRegionAtNonZeroOffsets) {
  constexpr UINT source_width = 5;
  constexpr UINT source_height = 4;
  constexpr UINT destination_width = 8;
  constexpr UINT destination_height = 6;
  const std::array<std::uint32_t, source_width *source_height> source_data = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000005,
      0xff000011, 0xff000012, 0xff000013, 0xff000014, 0xff000015,
      0xff000021, 0xff000022, 0xff000023, 0xff000024, 0xff000025,
      0xff000031, 0xff000032, 0xff000033, 0xff000034, 0xff000035,
  };
  std::array<std::uint32_t, destination_width * destination_height>
      destination_initial;
  destination_initial.fill(0x7f334455);
  auto expected = destination_initial;
  for (UINT y = 0; y < 2; ++y) {
    for (UINT x = 0; x < 3; ++x) {
      expected[(y + 2) * destination_width + x + 3] =
          source_data[(y + 1) * source_width + x + 1];
    }
  }

  ComPtr<ID3D12Resource> source = context_.CreateTexture2D(
      source_width, source_height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ComPtr<ID3D12Resource> destination = context_.CreateTexture2D(
      destination_width, destination_height, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(source);
  ASSERT_TRUE(destination);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      source.get(), source_data.data(), source_width * sizeof(std::uint32_t),
      source_data.size() * sizeof(std::uint32_t))));
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      destination.get(), destination_initial.data(),
      destination_width * sizeof(std::uint32_t),
      expected.size() * sizeof(std::uint32_t))));

  D3D12_TEXTURE_COPY_LOCATION source_location = {};
  source_location.pResource = source.get();
  source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION destination_location = {};
  destination_location.pResource = destination.get();
  destination_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_BOX source_box = {1, 1, 0, 4, 3, 1};
  context_.list()->CopyTextureRegion(&destination_location, 3, 2, 0,
                                     &source_location, &source_box);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  ASSERT_EQ(readback.width, destination_width);
  ASSERT_EQ(readback.height, destination_height);
  for (UINT y = 0; y < destination_height; ++y) {
    const auto *row = readback.data.data() + y * readback.row_pitch;
    EXPECT_EQ(std::memcmp(row, expected.data() + y * destination_width,
                          destination_width * sizeof(std::uint32_t)),
              0)
        << "row " << y;
  }
}

TEST_F(D3D12QueueSpec, ReportsTimestampFrequency) {
  UINT64 frequency = 0;
  ASSERT_TRUE(SUCCEEDED(context_.queue()->GetTimestampFrequency(&frequency)));
  EXPECT_GT(frequency, 0ull);
}

TEST_F(D3D12QueueSpec, PreservesTextureUploadAcrossSubmissions) {
  const std::array<std::uint32_t, 16> expected = {
      0xff000001, 0xff000002, 0xff000003, 0xff000004, 0xff000011, 0xff000012,
      0xff000013, 0xff000014, 0xff000021, 0xff000022, 0xff000023, 0xff000024,
      0xff000031, 0xff000032, 0xff000033, 0xff000034,
  };
  ComPtr<ID3D12Resource> texture = context_.CreateTexture2D(
      4, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(texture);
  ASSERT_TRUE(SUCCEEDED(context_.UploadTextureAndReset(
      texture.get(), expected.data(), 4 * sizeof(std::uint32_t),
      sizeof(expected))));

  D3D12TestContext::Transition(context_.list(), texture.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);
  TextureReadback readback;
  ASSERT_TRUE(SUCCEEDED(context_.ReadbackTexture(texture.get(), &readback)));
  ASSERT_EQ(readback.width, 4u);
  ASSERT_EQ(readback.height, 4u);
  for (UINT y = 0; y < readback.height; ++y) {
    for (UINT x = 0; x < readback.width; ++x) {
      std::uint32_t actual = 0;
      std::memcpy(&actual,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(actual),
                  sizeof(actual));
      EXPECT_EQ(actual, expected[y * readback.width + x]);
    }
  }
}

TEST_F(D3D12QueueSpec, ResolvesFullSourceRegionIntoLargerDestination) {
  constexpr UINT kSampleCount = 4;
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
  quality.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  quality.SampleCount = kSampleCount;
  if (FAILED(context_.device()->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality,
          sizeof(quality))) ||
      !quality.NumQualityLevels)
    GTEST_SKIP() << "4x MSAA is unavailable";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC source_desc = {};
  source_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  source_desc.Width = 4;
  source_desc.Height = 4;
  source_desc.DepthOrArraySize = 1;
  source_desc.MipLevels = 1;
  source_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  source_desc.SampleDesc.Count = kSampleCount;
  source_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  source_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear_value = {};
  clear_value.Format = source_desc.Format;
  clear_value.Color[0] = 0.25f;
  clear_value.Color[1] = 0.5f;
  clear_value.Color[2] = 0.75f;
  clear_value.Color[3] = 1.0f;

  ComPtr<ID3D12Resource> source;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &source_desc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
      __uuidof(ID3D12Resource), reinterpret_cast<void **>(source.put()))));

  auto destination_desc = source_desc;
  destination_desc.Width = 8;
  destination_desc.Height = 8;
  destination_desc.SampleDesc.Count = 1;
  destination_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> destination;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &destination_desc,
      D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, __uuidof(ID3D12Resource),
      reinterpret_cast<void **>(destination.put()))));

  auto rtv_heap =
      context_.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  ASSERT_TRUE(rtv_heap);
  const auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
  context_.device()->CreateRenderTargetView(source.get(), nullptr, rtv);
  context_.list()->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  D3D12TestContext::Transition(context_.list(), source.get(),
                               D3D12_RESOURCE_STATE_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);

  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_TRUE(SUCCEEDED(
      context_.list()->QueryInterface(__uuidof(ID3D12GraphicsCommandList1),
                                      reinterpret_cast<void **>(list1.put()))));
  list1->ResolveSubresourceRegion(destination.get(), 0, 0, 0, source.get(), 0,
                                  nullptr, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  D3D12_RESOLVE_MODE_AVERAGE);
  D3D12TestContext::Transition(context_.list(), destination.get(),
                               D3D12_RESOURCE_STATE_RESOLVE_DEST,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  TextureReadback readback;
  ASSERT_TRUE(
      SUCCEEDED(context_.ReadbackTexture(destination.get(), &readback)));
  ASSERT_EQ(readback.width, 8u);
  ASSERT_EQ(readback.height, 8u);
  for (UINT y = 0; y < 4; ++y) {
    for (UINT x = 0; x < 4; ++x) {
      std::uint32_t pixel = 0;
      std::memcpy(&pixel,
                  readback.data.data() + y * readback.row_pitch +
                      x * sizeof(pixel),
                  sizeof(pixel));
      EXPECT_TRUE(ColorsMatch(pixel, 0xffbf8040, 2));
    }
  }
}

TEST_F(D3D12QueueSpec, FailsCloseForUnadvertisedCommandFeatures) {
  ComPtr<ID3D12GraphicsCommandList1> list1;
  ASSERT_TRUE(SUCCEEDED(
      context_.list()->QueryInterface(__uuidof(ID3D12GraphicsCommandList1),
                                      reinterpret_cast<void **>(list1.put()))));

  std::array<std::uint32_t, 64> atomic_data = {};
  auto atomic_source = context_.CreateUploadBuffer(
      sizeof(atomic_data), atomic_data.data(), sizeof(atomic_data));
  auto atomic_destination =
      context_.CreateBuffer(sizeof(atomic_data), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
  auto atomic_dependent =
      context_.CreateBuffer(sizeof(atomic_data), D3D12_HEAP_TYPE_DEFAULT,
                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(atomic_source);
  ASSERT_TRUE(atomic_destination);
  ASSERT_TRUE(atomic_dependent);
  ID3D12Resource *dependent_resources[] = {atomic_dependent.get()};
  D3D12_SUBRESOURCE_RANGE_UINT64 dependent_ranges[] = {{0, {0, sizeof(UINT)}}};
  list1->AtomicCopyBufferUINT(atomic_destination.get(), 0, atomic_source.get(),
                              0, 1, dependent_resources, dependent_ranges);
  EXPECT_EQ(context_.list()->Close(), E_NOTIMPL);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  D3D12_STREAM_OUTPUT_BUFFER_VIEW stream_output = {};
  stream_output.BufferLocation = atomic_destination->GetGPUVirtualAddress();
  stream_output.SizeInBytes = sizeof(atomic_data);
  stream_output.BufferFilledSizeLocation =
      atomic_dependent->GetGPUVirtualAddress();
  context_.list()->SOSetTargets(0, 1, &stream_output);
  EXPECT_EQ(context_.list()->Close(), E_NOTIMPL);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  list1->OMSetDepthBounds(0.25f, 0.75f);
  EXPECT_EQ(context_.list()->Close(), E_NOTIMPL);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  auto decompress_source =
      context_.CreateTexture2D(16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                               D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  auto decompress_destination = context_.CreateTexture2D(
      16, 16, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_RESOLVE_DEST);
  ASSERT_TRUE(decompress_source);
  ASSERT_TRUE(decompress_destination);
  list1->ResolveSubresourceRegion(
      decompress_destination.get(), 0, 0, 0, decompress_source.get(), 0,
      nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_DECOMPRESS);
  EXPECT_EQ(context_.list()->Close(), E_NOTIMPL);
  ASSERT_TRUE(SUCCEEDED(context_.ResetCommandList()));

  // The default depth-bounds state remains a valid no-op when the feature is
  // not advertised.
  list1->OMSetDepthBounds(0.0f, 1.0f);
  EXPECT_EQ(context_.list()->Close(), S_OK);
}

TEST_F(D3D12QueueSpec, ResolveQueryRecordDoesNotRetainItsCommandList) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void **>(allocator.put()))));
  ComPtr<ID3D12GraphicsCommandList> list;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr,
      __uuidof(ID3D12GraphicsCommandList),
      reinterpret_cast<void **>(list.put()))));
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  query_desc.Count = 1;
  ComPtr<ID3D12QueryHeap> query_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
      &query_desc, __uuidof(ID3D12QueryHeap),
      reinterpret_cast<void **>(query_heap.put()))));
  ComPtr<ID3D12Resource> result = context_.CreateBuffer(
      sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(result);

  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(
      SUCCEEDED(list->SetPrivateDataInterface(__uuidof(IUnknown), probe)));
  probe->Release();
  list->ResolveQueryData(query_heap.get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1,
                         result.get(), 0);
  ASSERT_TRUE(SUCCEEDED(list->Close()));
  list.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

TEST_F(D3D12QueueSpec, EndsAndResolvesTimestampQuery) {
  D3D12_QUERY_HEAP_DESC query_desc = {};
  query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  query_desc.Count = 2;
  ComPtr<ID3D12QueryHeap> query_heap;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
      &query_desc, __uuidof(ID3D12QueryHeap),
      reinterpret_cast<void **>(query_heap.put()))));

  ComPtr<ID3D12Resource> result = context_.CreateBuffer(
      sizeof(UINT64), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  ASSERT_TRUE(result);

  context_.list()->EndQuery(query_heap.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  context_.list()->ResolveQueryData(query_heap.get(),
                                    D3D12_QUERY_TYPE_TIMESTAMP, 0, 1,
                                    result.get(), 0);
  EXPECT_TRUE(SUCCEEDED(context_.ExecuteAndWait()));

  UINT64 *timestamp = nullptr;
  D3D12_RANGE read_range = {0, sizeof(*timestamp)};
  ASSERT_TRUE(SUCCEEDED(result->Map(
      0, &read_range, reinterpret_cast<void **>(&timestamp))));
  ASSERT_NE(timestamp, nullptr);
  EXPECT_NE(*timestamp, ~UINT64{0});
  D3D12_RANGE written_range = {};
  result->Unmap(0, &written_range);
}

TEST_F(D3D12QueueSpec, ReleasingQueueCancelsAnUnresolvedFenceWait) {
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandQueue(
      &queue_desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void **>(queue.put()))));
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));

  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(
      SUCCEEDED(queue->SetPrivateDataInterface(__uuidof(IUnknown), probe)));
  probe->Release();
  ASSERT_TRUE(SUCCEEDED(queue->Wait(fence.get(), 1)));
  Sleep(50);
  queue.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

TEST_F(D3D12QueueSpec, QueueDestructionCanRaceFenceWaitCallbackArming) {
  ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
      reinterpret_cast<void **>(fence.put()))));

  // Immediate destruction deliberately races the submission worker between
  // observing the queued Wait and registering its CPU completion callback.
  // Repetition makes the narrow lifetime window a practical ASan/TSan stress
  // scenario without relying on implementation sleeps.
  for (UINT iteration = 0; iteration < 64; ++iteration) {
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandQueue(
        &queue_desc, __uuidof(ID3D12CommandQueue),
        reinterpret_cast<void **>(queue.put()))));
    ASSERT_TRUE(SUCCEEDED(queue->Wait(fence.get(), iteration + 1)));
    queue.reset();
  }
}

} // namespace
