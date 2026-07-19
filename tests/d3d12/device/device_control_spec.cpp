#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <dxgi1_3.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::CreateIsolatedD3D12Device;
using dxmt::test::D3D12TestContext;

class LifetimeOwner final : public ID3D12LifetimeOwner {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != __uuidof(IUnknown) && iid != __uuidof(ID3D12LifetimeOwner))
      return E_NOINTERFACE;
    *object = static_cast<ID3D12LifetimeOwner *>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = --ref_count_;
    if (!count)
      delete this;
    return count;
  }

  void STDMETHODCALLTYPE
  LifetimeStateUpdated(D3D12_LIFETIME_STATE state) override {
    last_state_ = state;
  }

private:
  std::atomic<ULONG> ref_count_{1};
  D3D12_LIFETIME_STATE last_state_ = D3D12_LIFETIME_STATE_IN_USE;
};

class DeviceControlSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  template <typename Interface> ComPtr<Interface> QueryDevice() {
    ComPtr<Interface> result;
    EXPECT_EQ(context_.device()->QueryInterface(
                  __uuidof(Interface),
                  reinterpret_cast<void **>(result.put())),
              S_OK);
    return result;
  }

  void ExpectCopyExecution() {
    constexpr std::array<std::uint32_t, 4> expected = {
        0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u};
    auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                               sizeof(expected));
    auto destination = context_.CreateBuffer(
        sizeof(expected), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(destination);
    context_.list()->CopyBufferRegion(destination.get(), 0, upload.get(), 0,
                                      sizeof(expected));
    D3D12TestContext::Transition(context_.list(), destination.get(),
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);

    std::vector<std::uint8_t> actual;
    ASSERT_EQ(context_.ReadbackBuffer(destination.get(), sizeof(expected),
                                      &actual),
              S_OK);
    ASSERT_EQ(actual.size(), sizeof(expected));
    EXPECT_EQ(std::memcmp(actual.data(), expected.data(), sizeof(expected)), 0);
  }

  D3D12TestContext context_;
};

TEST_F(DeviceControlSpec, CustomHeapPropertiesMatchAdvertisedArchitecture) {
  D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
  architecture.NodeIndex = 0;
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_ARCHITECTURE, &architecture,
                sizeof(architecture)),
            S_OK);

  struct ExpectedProperties {
    D3D12_HEAP_TYPE input_type;
    D3D12_CPU_PAGE_PROPERTY cpu_page_property;
    D3D12_MEMORY_POOL memory_pool;
  };
  const std::array expected = {
      ExpectedProperties{
          D3D12_HEAP_TYPE_DEFAULT,
          D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE,
          architecture.UMA ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1},
      ExpectedProperties{
          D3D12_HEAP_TYPE_UPLOAD,
          architecture.CacheCoherentUMA ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                                        : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE,
          D3D12_MEMORY_POOL_L0},
      ExpectedProperties{D3D12_HEAP_TYPE_READBACK,
                         D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
                         D3D12_MEMORY_POOL_L0},
  };

  for (const auto &test : expected) {
    for (const UINT node_mask : {0u, 1u}) {
      const auto properties =
          context_.device()->GetCustomHeapProperties(node_mask,
                                                      test.input_type);
      EXPECT_EQ(properties.Type, D3D12_HEAP_TYPE_CUSTOM);
      EXPECT_EQ(properties.CPUPageProperty, test.cpu_page_property);
      EXPECT_EQ(properties.MemoryPoolPreference, test.memory_pool);
      EXPECT_EQ(properties.CreationNodeMask, 1u);
      EXPECT_EQ(properties.VisibleNodeMask, 1u);
    }
  }
}

DXMT_SERIAL_TEST_F(DeviceControlSpec,
                   StablePowerStateReflectsDeveloperModeCapability) {
  const HRESULT hr = context_.device()->SetStablePowerState(FALSE);
  if (hr == S_OK) {
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    ExpectCopyExecution();
    return;
  }

  // The public contract removes the device when Windows developer mode is
  // disabled. Keep this case in its own worker so the intentional removal
  // cannot contaminate otherwise independent tests.
  EXPECT_EQ(hr, E_FAIL);
  EXPECT_HRESULT_FAILED(context_.device()->GetDeviceRemovedReason());
}

TEST_F(DeviceControlSpec, LifetimeTrackerUnsupportedPathClearsOutput) {
  auto device5 = QueryDevice<ID3D12Device5>();
  ASSERT_TRUE(device5);

  auto *tracker = reinterpret_cast<ID3D12LifetimeTracker *>(uintptr_t{1});
  EXPECT_EQ(device5->CreateLifetimeTracker(
                nullptr, __uuidof(ID3D12LifetimeTracker),
                reinterpret_cast<void **>(&tracker)),
            E_INVALIDARG);
  EXPECT_EQ(tracker, nullptr);

  ComPtr<ID3D12LifetimeOwner> owner(new LifetimeOwner());
  tracker = reinterpret_cast<ID3D12LifetimeTracker *>(uintptr_t{1});
  EXPECT_EQ(device5->CreateLifetimeTracker(
                owner.get(), __uuidof(ID3D12LifetimeTracker),
                reinterpret_cast<void **>(&tracker)),
            E_NOTIMPL);
  EXPECT_EQ(tracker, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  ExpectCopyExecution();
}

TEST_F(DeviceControlSpec,
       DriverIdentifierStatusMatchesUnsupportedRaytracingCapability) {
  auto device5 = QueryDevice<ID3D12Device5>();
  ASSERT_TRUE(device5);
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
  ASSERT_EQ(context_.device()->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)),
            S_OK);
  ASSERT_EQ(options.RaytracingTier,
            D3D12_RAYTRACING_TIER_NOT_SUPPORTED);

  D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER identifier = {};
  EXPECT_EQ(device5->CheckDriverMatchingIdentifier(
                D3D12_SERIALIZED_DATA_RAYTRACING_ACCELERATION_STRUCTURE,
                &identifier),
            D3D12_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(DeviceControlSpec, DxgiGpuPriorityRoundTripsAndRejectsInvalidValues) {
  auto dxgi_device = QueryDevice<IDXGIDevice>();
  ASSERT_TRUE(dxgi_device);

  INT priority = INT_MAX;
  EXPECT_EQ(dxgi_device->GetGPUThreadPriority(nullptr), E_POINTER);
  ASSERT_EQ(dxgi_device->GetGPUThreadPriority(&priority), S_OK);
  EXPECT_EQ(priority, 0);

  for (const INT valid_priority : {-7, 7, 0}) {
    ASSERT_EQ(dxgi_device->SetGPUThreadPriority(valid_priority), S_OK);
    priority = INT_MAX;
    ASSERT_EQ(dxgi_device->GetGPUThreadPriority(&priority), S_OK);
    EXPECT_EQ(priority, valid_priority);
  }

  for (const INT invalid_priority : {-8, 8}) {
    EXPECT_EQ(dxgi_device->SetGPUThreadPriority(invalid_priority),
              E_INVALIDARG);
    priority = INT_MAX;
    ASSERT_EQ(dxgi_device->GetGPUThreadPriority(&priority), S_OK);
    EXPECT_EQ(priority, 0);
  }
}

TEST_F(DeviceControlSpec, DxgiObjectSharesIdentityStateAndAdapterParent) {
  auto dxgi_object = QueryDevice<IDXGIObject>();
  auto dxgi_device = QueryDevice<IDXGIDevice3>();
  ASSERT_TRUE(dxgi_object);
  ASSERT_TRUE(dxgi_device);

  ComPtr<IUnknown> d3d_identity;
  ComPtr<IUnknown> dxgi_identity;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(d3d_identity.put())),
            S_OK);
  ASSERT_EQ(dxgi_device->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(dxgi_identity.put())),
            S_OK);
  EXPECT_EQ(d3d_identity.get(), dxgi_identity.get());

  constexpr GUID data_key = {
      0xb0499ce9,
      0x5f91,
      0x44fb,
      {0x86, 0x60, 0xef, 0x37, 0x17, 0x78, 0xe3, 0x1a}};
  constexpr std::uint32_t expected = 0x10203040u;
  ASSERT_EQ(dxgi_object->SetPrivateData(data_key, sizeof(expected), &expected),
            S_OK);
  std::uint32_t actual = 0;
  UINT actual_size = sizeof(actual);
  ASSERT_EQ(context_.device()->GetPrivateData(data_key, &actual_size, &actual),
            S_OK);
  EXPECT_EQ(actual_size, sizeof(actual));
  EXPECT_EQ(actual, expected);

  EXPECT_EQ(dxgi_device->GetAdapter(nullptr), E_INVALIDARG);
  ComPtr<IDXGIAdapter> adapter;
  ASSERT_EQ(dxgi_device->GetAdapter(adapter.put()), S_OK);
  ASSERT_TRUE(adapter);
  ComPtr<IDXGIAdapter> parent;
  ASSERT_EQ(dxgi_object->GetParent(
                __uuidof(IDXGIAdapter),
                reinterpret_cast<void **>(parent.put())),
            S_OK);
  ASSERT_TRUE(parent);

  ComPtr<IUnknown> adapter_identity;
  ComPtr<IUnknown> parent_identity;
  ASSERT_EQ(adapter->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(adapter_identity.put())),
            S_OK);
  ASSERT_EQ(parent->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(parent_identity.put())),
            S_OK);
  EXPECT_EQ(adapter_identity.get(), parent_identity.get());

  ComPtr<IDXGIAdapter1> adapter1;
  ASSERT_EQ(adapter->QueryInterface(
                __uuidof(IDXGIAdapter1),
                reinterpret_cast<void **>(adapter1.put())),
            S_OK);
  DXGI_ADAPTER_DESC1 adapter_desc = {};
  ASSERT_EQ(adapter1->GetDesc1(&adapter_desc), S_OK);
  const LUID device_luid = context_.device()->GetAdapterLuid();
  EXPECT_EQ(adapter_desc.AdapterLuid.LowPart, device_luid.LowPart);
  EXPECT_EQ(adapter_desc.AdapterLuid.HighPart, device_luid.HighPart);

  void *unsupported = reinterpret_cast<void *>(uintptr_t{1});
  EXPECT_EQ(dxgi_object->GetParent(__uuidof(ID3D12Fence), &unsupported),
            E_NOINTERFACE);
  EXPECT_EQ(unsupported, nullptr);
}

TEST_F(DeviceControlSpec, DxgiSurfaceFailureClearsOutput) {
  auto dxgi_device = QueryDevice<IDXGIDevice>();
  ASSERT_TRUE(dxgi_device);

  DXGI_SURFACE_DESC desc = {};
  desc.Width = 4;
  desc.Height = 4;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  auto *surface = reinterpret_cast<IDXGISurface *>(uintptr_t{1});
  EXPECT_EQ(dxgi_device->CreateSurface(
                &desc, 1, DXGI_USAGE_RENDER_TARGET_OUTPUT, nullptr, &surface),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(surface, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(DeviceControlSpec, DxgiFrameLatencyEnforcesBoundsAndResetValue) {
  auto dxgi_device = QueryDevice<IDXGIDevice1>();
  ASSERT_TRUE(dxgi_device);

  UINT latency = UINT_MAX;
  EXPECT_EQ(dxgi_device->GetMaximumFrameLatency(nullptr),
            DXGI_ERROR_INVALID_CALL);
  ASSERT_EQ(dxgi_device->GetMaximumFrameLatency(&latency), S_OK);
  EXPECT_EQ(latency, 3u);

  for (const UINT valid_latency : {1u, 16u}) {
    ASSERT_EQ(dxgi_device->SetMaximumFrameLatency(valid_latency), S_OK);
    latency = UINT_MAX;
    ASSERT_EQ(dxgi_device->GetMaximumFrameLatency(&latency), S_OK);
    EXPECT_EQ(latency, valid_latency);
  }

  EXPECT_EQ(dxgi_device->SetMaximumFrameLatency(17),
            DXGI_ERROR_INVALID_CALL);
  latency = UINT_MAX;
  ASSERT_EQ(dxgi_device->GetMaximumFrameLatency(&latency), S_OK);
  EXPECT_EQ(latency, 16u);

  ASSERT_EQ(dxgi_device->SetMaximumFrameLatency(0), S_OK);
  latency = UINT_MAX;
  ASSERT_EQ(dxgi_device->GetMaximumFrameLatency(&latency), S_OK);
  EXPECT_EQ(latency, 3u);
}

TEST_F(DeviceControlSpec, DxgiResourceControlsRejectInvalidInputs) {
  auto dxgi_device = QueryDevice<IDXGIDevice3>();
  ASSERT_TRUE(dxgi_device);

  DXGI_RESIDENCY residency = DXGI_RESIDENCY_EVICTED_TO_DISK;
  IUnknown *unknown = context_.device();
  EXPECT_EQ(dxgi_device->QueryResourceResidency(nullptr, &residency, 1),
            E_INVALIDARG);
  EXPECT_EQ(residency, DXGI_RESIDENCY_EVICTED_TO_DISK);
  EXPECT_EQ(dxgi_device->QueryResourceResidency(&unknown, nullptr, 1),
            E_INVALIDARG);

  EXPECT_EQ(dxgi_device->OfferResources(
                0, nullptr, static_cast<DXGI_OFFER_RESOURCE_PRIORITY>(0)),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device->OfferResources(
                1, nullptr, DXGI_OFFER_RESOURCE_PRIORITY_NORMAL),
            E_INVALIDARG);
  EXPECT_EQ(dxgi_device->ReclaimResources(1, nullptr, nullptr), E_INVALIDARG);
  EXPECT_EQ(dxgi_device->EnqueueSetEvent(nullptr), E_INVALIDARG);

  dxgi_device->Trim();
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST(DeviceRemovalSpec, InitialReasonIsSuccess) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);
  EXPECT_EQ(device->GetDeviceRemovedReason(), S_OK);
  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(device.get()), S_OK);
  EXPECT_EQ(context.device()->GetDeviceRemovedReason(), S_OK);
  // Queue remains healthy for ordinary fence work before any removal.
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_EQ(context.queue()->Signal(fence.get(), 1), S_OK);
  ASSERT_EQ(context.SignalAndWait(), S_OK);
  EXPECT_GE(fence->GetCompletedValue(), 1ull);
  EXPECT_EQ(device->GetDeviceRemovedReason(), S_OK);
}

TEST(DeviceRemovalSpec, ExplicitRemovalIsStickyAndRejectsQueueWork) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);

  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(device.get()), S_OK);
  ASSERT_EQ(context.list()->Close(), S_OK);

  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  ASSERT_TRUE(device5);

  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);
  ASSERT_TRUE(fence);

  HANDLE pending_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE post_removal_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ASSERT_NE(pending_event, nullptr);
  ASSERT_NE(post_removal_event, nullptr);

  EXPECT_EQ(device->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(fence->SetEventOnCompletion(7, pending_event), S_OK);
  EXPECT_EQ(WaitForSingleObject(pending_event, 0), WAIT_TIMEOUT);
  EXPECT_EQ(context.queue()->Wait(fence.get(), 7), S_OK);
  device5->RemoveDevice();
  device5->RemoveDevice();
  EXPECT_EQ(device->GetDeviceRemovedReason(), DXGI_ERROR_DEVICE_REMOVED);
  EXPECT_EQ(fence->GetCompletedValue(), UINT64_MAX);
  EXPECT_EQ(WaitForSingleObject(pending_event, 0), WAIT_OBJECT_0);
  EXPECT_EQ(fence->SetEventOnCompletion(1, post_removal_event), S_OK);
  EXPECT_EQ(WaitForSingleObject(post_removal_event, 0), WAIT_OBJECT_0);
  EXPECT_EQ(context.queue()->Signal(fence.get(), 1),
            DXGI_ERROR_DEVICE_REMOVED);
  EXPECT_EQ(context.queue()->Wait(fence.get(), 1),
            DXGI_ERROR_DEVICE_REMOVED);

  ID3D12CommandList *lists[] = {context.list()};
  context.queue()->ExecuteCommandLists(1, lists);
  EXPECT_EQ(context.allocator()->Reset(), S_OK);

  auto fresh_device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(fresh_device);
  EXPECT_EQ(fresh_device->GetDeviceRemovedReason(), S_OK);
  ComPtr<ID3D12Fence> fresh_fence;
  EXPECT_EQ(fresh_device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fresh_fence.put())),
            S_OK);
  EXPECT_TRUE(fresh_fence);

  CloseHandle(post_removal_event);
  CloseHandle(pending_event);
}

TEST(DeviceRemovalSpec, PendingFenceWaitDoesNotHangAfterRemoval) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);
  D3D12TestContext context;
  ASSERT_EQ(context.Initialize(device.get()), S_OK);

  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                reinterpret_cast<void **>(fence.put())),
            S_OK);
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);
  ASSERT_EQ(fence->SetEventOnCompletion(100, event), S_OK);
  EXPECT_EQ(WaitForSingleObject(event, 0), WAIT_TIMEOUT);

  device5->RemoveDevice();
  EXPECT_EQ(device->GetDeviceRemovedReason(), DXGI_ERROR_DEVICE_REMOVED);
  EXPECT_EQ(fence->GetCompletedValue(), UINT64_MAX);
  EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
  CloseHandle(event);
}

TEST(DeviceRemovalSpec, DeviceAndQueueDestructionDoNotDeadlock) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);
  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                reinterpret_cast<void **>(fence.put())),
            S_OK);
  HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  ASSERT_NE(event, nullptr);

  {
    D3D12TestContext context;
    ASSERT_EQ(context.Initialize(device.get()), S_OK);
    ASSERT_EQ(fence->SetEventOnCompletion(42, event), S_OK);
    device5->RemoveDevice();
    EXPECT_EQ(WaitForSingleObject(event, 5000), WAIT_OBJECT_0);
    // Context/queue destructor runs at scope exit after removal.
  }

  fence.reset();
  device5.reset();
  device.reset();
  CloseHandle(event);
}

TEST(DeviceRemovalSpec,
     ConcurrentRemovalSignalsAllFenceRegistrationsAndReleasesCleanly) {
  constexpr UINT kFenceCount = 8;
  constexpr UINT kRemovalThreadCount = 4;
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);

  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  ASSERT_TRUE(device5);

  std::array<ComPtr<ID3D12Fence>, kFenceCount> fences;
  std::array<HANDLE, kFenceCount> events = {};
  std::array<HRESULT, kFenceCount> registrations = {};
  for (UINT index = 0; index < kFenceCount; ++index) {
    ASSERT_EQ(device->CreateFence(
                  index, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                  reinterpret_cast<void **>(fences[index].put())),
              S_OK);
    events[index] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(events[index], nullptr);
  }

  std::atomic_bool start = false;
  std::vector<std::thread> threads;
  threads.reserve(kFenceCount + kRemovalThreadCount);
  for (UINT index = 0; index < kFenceCount; ++index) {
    threads.emplace_back([&, index] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      registrations[index] =
          fences[index]->SetEventOnCompletion(UINT64_MAX - index,
                                               events[index]);
    });
  }
  for (UINT index = 0; index < kRemovalThreadCount; ++index) {
    threads.emplace_back([&] {
      while (!start.load(std::memory_order_acquire))
        std::this_thread::yield();
      device5->RemoveDevice();
    });
  }

  start.store(true, std::memory_order_release);
  for (auto &thread : threads)
    thread.join();
  for (const HRESULT registration : registrations)
    EXPECT_EQ(registration, S_OK);
  EXPECT_EQ(WaitForMultipleObjects(kFenceCount, events.data(), TRUE, 5000),
            WAIT_OBJECT_0);
  EXPECT_EQ(device->GetDeviceRemovedReason(), DXGI_ERROR_DEVICE_REMOVED);
  for (const auto &fence : fences)
    EXPECT_EQ(fence->GetCompletedValue(), UINT64_MAX);

  threads.clear();
  threads.reserve(kFenceCount);
  for (UINT index = 0; index < kFenceCount; ++index)
    threads.emplace_back([&, index] { fences[index].reset(); });
  for (auto &thread : threads)
    thread.join();

  for (const HANDLE event : events)
    CloseHandle(event);
  EXPECT_EQ(device->GetDeviceRemovedReason(), DXGI_ERROR_DEVICE_REMOVED);
}

#ifdef __ID3D12DeviceRemovedExtendedData2_INTERFACE_DEFINED__
TEST(DeviceDredSpec, UnavailableReportsTrackExplicitRemovalState) {
  auto device = CreateIsolatedD3D12Device();
  ASSERT_TRUE(device);

  ComPtr<ID3D12DeviceRemovedExtendedData2> dred2;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12DeviceRemovedExtendedData2),
                reinterpret_cast<void **>(dred2.put())),
            S_OK);
  ASSERT_TRUE(dred2);

  ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
  ComPtr<ID3D12DeviceRemovedExtendedData> dred;
  ASSERT_EQ(dred2->QueryInterface(
                __uuidof(ID3D12DeviceRemovedExtendedData1),
                reinterpret_cast<void **>(dred1.put())),
            S_OK);
  ASSERT_EQ(dred2->QueryInterface(
                __uuidof(ID3D12DeviceRemovedExtendedData),
                reinterpret_cast<void **>(dred.put())),
            S_OK);

  ComPtr<IUnknown> device_identity;
  ComPtr<IUnknown> dred_identity;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(device_identity.put())),
            S_OK);
  ASSERT_EQ(dred2->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(dred_identity.put())),
            S_OK);
  EXPECT_EQ(device_identity.get(), dred_identity.get());

  EXPECT_EQ(dred->GetAutoBreadcrumbsOutput(nullptr), E_INVALIDARG);
  EXPECT_EQ(dred->GetPageFaultAllocationOutput(nullptr), E_INVALIDARG);
  EXPECT_EQ(dred1->GetAutoBreadcrumbsOutput1(nullptr), E_INVALIDARG);
  EXPECT_EQ(dred1->GetPageFaultAllocationOutput1(nullptr), E_INVALIDARG);
  EXPECT_EQ(dred2->GetPageFaultAllocationOutput2(nullptr), E_INVALIDARG);

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
  breadcrumbs.pHeadAutoBreadcrumbNode =
      reinterpret_cast<const D3D12_AUTO_BREADCRUMB_NODE *>(uintptr_t{1});
  EXPECT_EQ(dred->GetAutoBreadcrumbsOutput(&breadcrumbs),
            DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
  EXPECT_EQ(breadcrumbs.pHeadAutoBreadcrumbNode, nullptr);

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs1 = {};
  breadcrumbs1.pHeadAutoBreadcrumbNode =
      reinterpret_cast<const D3D12_AUTO_BREADCRUMB_NODE1 *>(uintptr_t{1});
  EXPECT_EQ(dred1->GetAutoBreadcrumbsOutput1(&breadcrumbs1),
            DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
  EXPECT_EQ(breadcrumbs1.pHeadAutoBreadcrumbNode, nullptr);

  D3D12_DRED_PAGE_FAULT_OUTPUT page_fault = {};
  page_fault.PageFaultVA = 1;
  page_fault.pHeadExistingAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE *>(uintptr_t{1});
  page_fault.pHeadRecentFreedAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE *>(uintptr_t{1});
  EXPECT_EQ(dred->GetPageFaultAllocationOutput(&page_fault),
            DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
  EXPECT_EQ(page_fault.PageFaultVA, 0u);
  EXPECT_EQ(page_fault.pHeadExistingAllocationNode, nullptr);
  EXPECT_EQ(page_fault.pHeadRecentFreedAllocationNode, nullptr);

  D3D12_DRED_PAGE_FAULT_OUTPUT1 page_fault1 = {};
  page_fault1.PageFaultVA = 1;
  page_fault1.pHeadExistingAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE1 *>(uintptr_t{1});
  page_fault1.pHeadRecentFreedAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE1 *>(uintptr_t{1});
  EXPECT_EQ(dred1->GetPageFaultAllocationOutput1(&page_fault1),
            DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
  EXPECT_EQ(page_fault1.PageFaultVA, 0u);
  EXPECT_EQ(page_fault1.pHeadExistingAllocationNode, nullptr);
  EXPECT_EQ(page_fault1.pHeadRecentFreedAllocationNode, nullptr);

  D3D12_DRED_PAGE_FAULT_OUTPUT2 page_fault2 = {};
  page_fault2.PageFaultVA = 1;
  page_fault2.pHeadExistingAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE1 *>(uintptr_t{1});
  page_fault2.pHeadRecentFreedAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE1 *>(uintptr_t{1});
  page_fault2.PageFaultFlags =
      static_cast<D3D12_DRED_PAGE_FAULT_FLAGS>(UINT_MAX);
  EXPECT_EQ(dred2->GetPageFaultAllocationOutput2(&page_fault2),
            DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);
  EXPECT_EQ(page_fault2.PageFaultVA, 0u);
  EXPECT_EQ(page_fault2.pHeadExistingAllocationNode, nullptr);
  EXPECT_EQ(page_fault2.pHeadRecentFreedAllocationNode, nullptr);
  EXPECT_EQ(page_fault2.PageFaultFlags, D3D12_DRED_PAGE_FAULT_FLAGS_NONE);
  EXPECT_EQ(dred2->GetDeviceState(), D3D12_DRED_DEVICE_STATE_UNKNOWN);

  ComPtr<ID3D12Device5> device5;
  ASSERT_EQ(device->QueryInterface(
                __uuidof(ID3D12Device5),
                reinterpret_cast<void **>(device5.put())),
            S_OK);
  device5->RemoveDevice();
  EXPECT_EQ(dred2->GetDeviceState(), D3D12_DRED_DEVICE_STATE_FAULT);
  EXPECT_EQ(device->GetDeviceRemovedReason(), DXGI_ERROR_DEVICE_REMOVED);

  breadcrumbs.pHeadAutoBreadcrumbNode =
      reinterpret_cast<const D3D12_AUTO_BREADCRUMB_NODE *>(uintptr_t{1});
  EXPECT_EQ(dred->GetAutoBreadcrumbsOutput(&breadcrumbs),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(breadcrumbs.pHeadAutoBreadcrumbNode, nullptr);
  page_fault2.PageFaultVA = 1;
  page_fault2.pHeadExistingAllocationNode =
      reinterpret_cast<const D3D12_DRED_ALLOCATION_NODE1 *>(uintptr_t{1});
  EXPECT_EQ(dred2->GetPageFaultAllocationOutput2(&page_fault2),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(page_fault2.PageFaultVA, 0u);
  EXPECT_EQ(page_fault2.pHeadExistingAllocationNode, nullptr);
}
#endif

} // namespace
