#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwchar>

// Public D3D11.4 fence synchronization coverage. Devices, contexts, fences,
// and events are test-local; named objects combine the process ID, a monotonic
// clock sample, and an atomic sequence so separate scheduler invocations cannot
// collide. Event waits observe queue completion rather than relying on sleeps
// or shared queue ordering, so these cases are parallel-safe.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kOpenedFenceDataKey = {
    0x8cf56a31,
    0xe10d,
    0x4af1,
    {0x93, 0x9a, 0xe5, 0x6d, 0x1b, 0xdf, 0x0b, 0x82}};

const dxmt::test::TestCostRegistration kSharedFenceSynchronizationCost(
    "D3D11FenceSynchronizationContractSpec."
    "SharedFenceSynchronizesBidirectionallyAcrossDevices",
    dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kNamedDuplicatedFenceCost("D3D11FenceSynchronizationContractSpec."
                              "OpensNamedFenceThroughDuplicatedHandle",
                              dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kReleasedCreatorFenceCost("D3D11FenceSynchronizationContractSpec."
                              "OpensHandleAfterCreatorFenceRelease",
                              dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kMultipleHandleFenceCost("D3D11FenceSynchronizationContractSpec."
                             "MultipleHandlesPreserveSharedIdentity",
                             dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kRepeatedOpenFenceCost("D3D11FenceSynchronizationContractSpec."
                           "RepeatedOpenCreatesIndependentWrappers",
                           dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kForeignFenceCost("D3D11FenceSynchronizationContractSpec."
                      "RejectsFenceOwnedByDifferentDevice",
                      dxmt::test::kResourceTestCost);
const dxmt::test::TestCostRegistration
    kNullFenceCost("D3D11FenceSynchronizationContractSpec."
                   "RejectsNullFenceSynchronization",
                   dxmt::test::kResourceTestCost);

std::atomic<std::uint32_t> g_next_fence_name_id{0};

struct ScopedEvent {
  ScopedEvent() : handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

  ScopedEvent(const ScopedEvent &) = delete;
  ScopedEvent &operator=(const ScopedEvent &) = delete;

  ~ScopedEvent() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

struct ScopedNtHandle {
  ScopedNtHandle() = default;
  ScopedNtHandle(const ScopedNtHandle &) = delete;
  ScopedNtHandle &operator=(const ScopedNtHandle &) = delete;

  ~ScopedNtHandle() {
    if (handle)
      CloseHandle(handle);
  }

  HANDLE handle = nullptr;
};

class D3D11FenceSynchronizationContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_EQ(
        context_.device()->QueryInterface(
            __uuidof(ID3D11Device5), reinterpret_cast<void **>(device5_.put())),
        S_OK);

    ComPtr<ID3D11DeviceContext> immediate;
    context_.device()->GetImmediateContext(immediate.put());
    ASSERT_NE(immediate.get(), nullptr);
    ASSERT_EQ(
        immediate->QueryInterface(__uuidof(ID3D11DeviceContext4),
                                  reinterpret_cast<void **>(immediate4_.put())),
        S_OK);
    ASSERT_NE(immediate4_.get(), nullptr);
  }

  ComPtr<ID3D11Fence> CreateFence(UINT64 initial_value = 0) {
    ComPtr<ID3D11Fence> fence;
    EXPECT_EQ(device5_->CreateFence(initial_value, D3D11_FENCE_FLAG_NONE,
                                    __uuidof(ID3D11Fence),
                                    reinterpret_cast<void **>(fence.put())),
              S_OK);
    return fence;
  }

  D3D11TestContext context_;
  ComPtr<ID3D11Device5> device5_;
  ComPtr<ID3D11DeviceContext4> immediate4_;
};

TEST_F(D3D11FenceSynchronizationContractSpec,
       SignalsEventForAlreadyCompletedValue) {
  ComPtr<ID3D11Fence> fence = CreateFence(5);
  ASSERT_NE(fence.get(), nullptr);
  ScopedEvent event;
  ASSERT_NE(event.handle, nullptr);

  ASSERT_EQ(fence->SetEventOnCompletion(5, event.handle), S_OK);
  EXPECT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_EQ(fence->GetCompletedValue(), 5u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       ImmediateContextSignalAdvancesCompletedValue) {
  constexpr std::array<UINT64, 3> kSignalValues = {1, 7, 1024};
  ComPtr<ID3D11Fence> fence = CreateFence();
  ASSERT_NE(fence.get(), nullptr);

  for (const UINT64 value : kSignalValues) {
    ScopedEvent event;
    ASSERT_NE(event.handle, nullptr);
    ASSERT_EQ(fence->SetEventOnCompletion(value, event.handle), S_OK);
    EXPECT_EQ(WaitForSingleObject(event.handle, 0), WAIT_TIMEOUT);
    ASSERT_EQ(immediate4_->Signal(fence.get(), value), S_OK);
    ASSERT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
    EXPECT_GE(fence->GetCompletedValue(), value);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       ImmediateContextWaitOrdersFollowingSignal) {
  ComPtr<ID3D11Fence> gate = CreateFence(3);
  ComPtr<ID3D11Fence> completion = CreateFence();
  ASSERT_NE(gate.get(), nullptr);
  ASSERT_NE(completion.get(), nullptr);
  ScopedEvent event;
  ASSERT_NE(event.handle, nullptr);
  ASSERT_EQ(completion->SetEventOnCompletion(9, event.handle), S_OK);

  ASSERT_EQ(immediate4_->Wait(gate.get(), 3), S_OK);
  ASSERT_EQ(immediate4_->Signal(completion.get(), 9), S_OK);
  ASSERT_EQ(WaitForSingleObject(event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(completion->GetCompletedValue(), 9u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec, SupportsZeroValueSignalAndWait) {
  ComPtr<ID3D11Fence> fence = CreateFence();
  ASSERT_NE(fence.get(), nullptr);

  EXPECT_EQ(immediate4_->Wait(fence.get(), 0), S_OK);
  EXPECT_EQ(immediate4_->Signal(fence.get(), 0), S_OK);
  EXPECT_EQ(fence->GetCompletedValue(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       DeferredContextRejectsSignalAndWait) {
  ComPtr<ID3D11DeviceContext> deferred;
  ASSERT_EQ(context_.device()->CreateDeferredContext(0, deferred.put()), S_OK);
  ASSERT_NE(deferred.get(), nullptr);
  ComPtr<ID3D11DeviceContext4> deferred4;
  ASSERT_EQ(
      deferred->QueryInterface(__uuidof(ID3D11DeviceContext4),
                               reinterpret_cast<void **>(deferred4.put())),
      S_OK);
  ASSERT_NE(deferred4.get(), nullptr);
  ComPtr<ID3D11Fence> fence = CreateFence();
  ASSERT_NE(fence.get(), nullptr);

  EXPECT_EQ(deferred4->Signal(fence.get(), 1), DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(deferred4->Wait(fence.get(), 1), DXGI_ERROR_INVALID_CALL);
  EXPECT_EQ(fence->GetCompletedValue(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       SharedFenceSynchronizesBidirectionallyAcrossDevices) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11Device5> second_device5;
  ComPtr<ID3D11DeviceContext4> second_context4;
  ASSERT_EQ(second_device->QueryInterface(
                __uuidof(ID3D11Device5),
                reinterpret_cast<void **>(second_device5.put())),
            S_OK);
  ASSERT_EQ(second_context->QueryInterface(
                __uuidof(ID3D11DeviceContext4),
                reinterpret_cast<void **>(second_context4.put())),
            S_OK);

  ComPtr<ID3D11Fence> creator_fence;
  ASSERT_EQ(
      device5_->CreateFence(3, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                            reinterpret_cast<void **>(creator_fence.put())),
      S_OK);
  ScopedNtHandle shared_handle;
  ASSERT_EQ(creator_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                              &shared_handle.handle),
            S_OK);
  ASSERT_NE(shared_handle.handle, nullptr);

  ComPtr<ID3D11Fence> opened_fence;
  ASSERT_EQ(second_device5->OpenSharedFence(
                shared_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(opened_fence.put())),
            S_OK);
  ASSERT_TRUE(opened_fence);
  EXPECT_EQ(opened_fence->GetCompletedValue(), 3u);
  ComPtr<ID3D11Device> opened_owner;
  opened_fence->GetDevice(opened_owner.put());
  EXPECT_EQ(opened_owner.get(), second_device.get());

  ScopedEvent opened_event;
  ASSERT_NE(opened_event.handle, nullptr);
  ASSERT_EQ(opened_fence->SetEventOnCompletion(11, opened_event.handle), S_OK);
  ASSERT_EQ(immediate4_->Signal(creator_fence.get(), 11), S_OK);
  ASSERT_EQ(WaitForSingleObject(opened_event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(opened_fence->GetCompletedValue(), 11u);

  ScopedEvent creator_event;
  ASSERT_NE(creator_event.handle, nullptr);
  ASSERT_EQ(creator_fence->SetEventOnCompletion(17, creator_event.handle),
            S_OK);
  ASSERT_EQ(second_context4->Signal(opened_fence.get(), 17), S_OK);
  ASSERT_EQ(WaitForSingleObject(creator_event.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(creator_fence->GetCompletedValue(), 17u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       OpensNamedFenceThroughDuplicatedHandle) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11Device5> second_device5;
  ASSERT_EQ(second_device->QueryInterface(
                __uuidof(ID3D11Device5),
                reinterpret_cast<void **>(second_device5.put())),
            S_OK);

  ComPtr<ID3D11Fence> creator_fence;
  ASSERT_EQ(
      device5_->CreateFence(29, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                            reinterpret_cast<void **>(creator_fence.put())),
      S_OK);

  std::array<wchar_t, MAX_PATH> shared_name = {};
  const std::uint32_t name_id =
      g_next_fence_name_id.fetch_add(1, std::memory_order_relaxed);
  const auto run_id = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const int name_length = std::swprintf(
      shared_name.data(), shared_name.size(),
      L"DXMT_D3D11_SharedFence_%lu_%llu_%u",
      static_cast<unsigned long>(GetCurrentProcessId()), run_id, name_id);
  ASSERT_GT(name_length, 0);
  ASSERT_LT(static_cast<std::size_t>(name_length), shared_name.size());

  ScopedNtHandle named_handle;
  ASSERT_EQ(creator_fence->CreateSharedHandle(
                nullptr, GENERIC_ALL, shared_name.data(), &named_handle.handle),
            S_OK);
  ASSERT_NE(named_handle.handle, nullptr);

  ScopedNtHandle duplicate_handle;
  ASSERT_TRUE(DuplicateHandle(GetCurrentProcess(), named_handle.handle,
                              GetCurrentProcess(), &duplicate_handle.handle, 0,
                              FALSE, DUPLICATE_SAME_ACCESS));
  ASSERT_NE(duplicate_handle.handle, nullptr);
  ASSERT_TRUE(CloseHandle(named_handle.handle));
  named_handle.handle = nullptr;

  ComPtr<ID3D11Fence> opened_fence;
  ASSERT_EQ(second_device5->OpenSharedFence(
                duplicate_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(opened_fence.put())),
            S_OK);
  ASSERT_TRUE(opened_fence);
  EXPECT_EQ(opened_fence->GetCompletedValue(), 29u);
  ComPtr<ID3D11Device> opened_owner;
  opened_fence->GetDevice(opened_owner.put());
  EXPECT_EQ(opened_owner.get(), second_device.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       OpensHandleAfterCreatorFenceRelease) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11Device5> second_device5;
  ComPtr<ID3D11DeviceContext4> second_context4;
  ASSERT_EQ(second_device->QueryInterface(
                __uuidof(ID3D11Device5),
                reinterpret_cast<void **>(second_device5.put())),
            S_OK);
  ASSERT_EQ(second_context->QueryInterface(
                __uuidof(ID3D11DeviceContext4),
                reinterpret_cast<void **>(second_context4.put())),
            S_OK);

  ComPtr<ID3D11Fence> creator_fence;
  ASSERT_EQ(
      device5_->CreateFence(41, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                            reinterpret_cast<void **>(creator_fence.put())),
      S_OK);
  ScopedNtHandle shared_handle;
  ASSERT_EQ(creator_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                              &shared_handle.handle),
            S_OK);
  ASSERT_NE(shared_handle.handle, nullptr);

  creator_fence.reset();
  ComPtr<ID3D11Fence> opened_fence;
  ASSERT_EQ(second_device5->OpenSharedFence(
                shared_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(opened_fence.put())),
            S_OK);
  ASSERT_TRUE(opened_fence);
  EXPECT_EQ(opened_fence->GetCompletedValue(), 41u);

  ASSERT_TRUE(CloseHandle(shared_handle.handle));
  shared_handle.handle = nullptr;
  ScopedEvent completion;
  ASSERT_NE(completion.handle, nullptr);
  ASSERT_EQ(opened_fence->SetEventOnCompletion(47, completion.handle), S_OK);
  ASSERT_EQ(second_context4->Signal(opened_fence.get(), 47), S_OK);
  ASSERT_EQ(WaitForSingleObject(completion.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(opened_fence->GetCompletedValue(), 47u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       MultipleHandlesPreserveSharedIdentity) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11Device5> second_device5;
  ComPtr<ID3D11DeviceContext4> second_context4;
  ASSERT_EQ(second_device->QueryInterface(
                __uuidof(ID3D11Device5),
                reinterpret_cast<void **>(second_device5.put())),
            S_OK);
  ASSERT_EQ(second_context->QueryInterface(
                __uuidof(ID3D11DeviceContext4),
                reinterpret_cast<void **>(second_context4.put())),
            S_OK);

  ComPtr<ID3D11Fence> creator_fence;
  ASSERT_EQ(
      device5_->CreateFence(53, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                            reinterpret_cast<void **>(creator_fence.put())),
      S_OK);
  ScopedNtHandle first_handle;
  ScopedNtHandle second_handle;
  ASSERT_EQ(creator_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                              &first_handle.handle),
            S_OK);
  ASSERT_EQ(creator_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                              &second_handle.handle),
            S_OK);
  ASSERT_NE(first_handle.handle, nullptr);
  ASSERT_NE(second_handle.handle, nullptr);

  ComPtr<ID3D11Fence> first_opened;
  ComPtr<ID3D11Fence> second_opened;
  ASSERT_EQ(second_device5->OpenSharedFence(
                first_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(first_opened.put())),
            S_OK);
  ASSERT_EQ(second_device5->OpenSharedFence(
                second_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(second_opened.put())),
            S_OK);
  ASSERT_TRUE(first_opened);
  ASSERT_TRUE(second_opened);
  EXPECT_EQ(first_opened->GetCompletedValue(), 53u);
  EXPECT_EQ(second_opened->GetCompletedValue(), 53u);

  ASSERT_TRUE(CloseHandle(first_handle.handle));
  first_handle.handle = nullptr;
  ASSERT_TRUE(CloseHandle(second_handle.handle));
  second_handle.handle = nullptr;
  creator_fence.reset();

  ScopedEvent completion;
  ASSERT_NE(completion.handle, nullptr);
  ASSERT_EQ(second_opened->SetEventOnCompletion(61, completion.handle), S_OK);
  ASSERT_EQ(second_context4->Signal(first_opened.get(), 61), S_OK);
  ASSERT_EQ(WaitForSingleObject(completion.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(first_opened->GetCompletedValue(), 61u);
  EXPECT_GE(second_opened->GetCompletedValue(), 61u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       RepeatedOpenCreatesIndependentWrappers) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11Device5> second_device5;
  ComPtr<ID3D11DeviceContext4> second_context4;
  ASSERT_EQ(second_device->QueryInterface(
                __uuidof(ID3D11Device5),
                reinterpret_cast<void **>(second_device5.put())),
            S_OK);
  ASSERT_EQ(second_context->QueryInterface(
                __uuidof(ID3D11DeviceContext4),
                reinterpret_cast<void **>(second_context4.put())),
            S_OK);

  ComPtr<ID3D11Fence> creator_fence;
  ASSERT_EQ(
      device5_->CreateFence(71, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence),
                            reinterpret_cast<void **>(creator_fence.put())),
      S_OK);
  ScopedNtHandle shared_handle;
  ASSERT_EQ(creator_fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr,
                                              &shared_handle.handle),
            S_OK);
  ASSERT_NE(shared_handle.handle, nullptr);

  ComPtr<ID3D11Fence> first_opened;
  ComPtr<ID3D11Fence> second_opened;
  ASSERT_EQ(second_device5->OpenSharedFence(
                shared_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(first_opened.put())),
            S_OK);
  ASSERT_EQ(second_device5->OpenSharedFence(
                shared_handle.handle, __uuidof(ID3D11Fence),
                reinterpret_cast<void **>(second_opened.put())),
            S_OK);
  ASSERT_TRUE(first_opened);
  ASSERT_TRUE(second_opened);

  ComPtr<IUnknown> first_identity;
  ComPtr<IUnknown> second_identity;
  ASSERT_EQ(
      first_opened->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(first_identity.put())),
      S_OK);
  ASSERT_EQ(
      second_opened->QueryInterface(
          __uuidof(IUnknown), reinterpret_cast<void **>(second_identity.put())),
      S_OK);
  EXPECT_NE(first_identity.get(), second_identity.get());

  constexpr std::array<std::uint8_t, 4> kPrivateData = {0x11, 0x32, 0x57, 0x9b};
  ASSERT_EQ(first_opened->SetPrivateData(
                kOpenedFenceDataKey, kPrivateData.size(), kPrivateData.data()),
            S_OK);
  UINT private_data_size = 0;
  EXPECT_EQ(second_opened->GetPrivateData(kOpenedFenceDataKey,
                                          &private_data_size, nullptr),
            DXGI_ERROR_NOT_FOUND);

  ScopedEvent completion;
  ASSERT_NE(completion.handle, nullptr);
  ASSERT_EQ(second_opened->SetEventOnCompletion(79, completion.handle), S_OK);
  ASSERT_EQ(second_context4->Signal(first_opened.get(), 79), S_OK);
  ASSERT_EQ(WaitForSingleObject(completion.handle, 5000), WAIT_OBJECT_0);
  EXPECT_GE(first_opened->GetCompletedValue(), 79u);
  EXPECT_GE(second_opened->GetCompletedValue(), 79u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec,
       RejectsFenceOwnedByDifferentDevice) {
  constexpr D3D_FEATURE_LEVEL kFeatureLevel = D3D_FEATURE_LEVEL_11_0;
  D3D_FEATURE_LEVEL chosen_level = D3D_FEATURE_LEVEL_9_1;
  ComPtr<ID3D11Device> second_device;
  ComPtr<ID3D11DeviceContext> second_context;
  ASSERT_EQ(D3D11CreateDevice(context_.adapter(), D3D_DRIVER_TYPE_UNKNOWN,
                              nullptr, 0, &kFeatureLevel, 1, D3D11_SDK_VERSION,
                              second_device.put(), &chosen_level,
                              second_context.put()),
            S_OK);
  ASSERT_EQ(chosen_level, kFeatureLevel);
  ComPtr<ID3D11DeviceContext4> second_context4;
  ASSERT_EQ(second_context->QueryInterface(
                __uuidof(ID3D11DeviceContext4),
                reinterpret_cast<void **>(second_context4.put())),
            S_OK);

  ComPtr<ID3D11Fence> foreign_fence = CreateFence();
  ASSERT_TRUE(foreign_fence);
  EXPECT_EQ(second_context4->Signal(foreign_fence.get(), 1), E_INVALIDARG);
  EXPECT_EQ(second_context4->Wait(foreign_fence.get(), 0), E_INVALIDARG);
  EXPECT_EQ(foreign_fence->GetCompletedValue(), 0u);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(second_device->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11FenceSynchronizationContractSpec, RejectsNullFenceSynchronization) {
  EXPECT_EQ(immediate4_->Signal(nullptr, 1), E_INVALIDARG);
  EXPECT_EQ(immediate4_->Wait(nullptr, 0), E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
