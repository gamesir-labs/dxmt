#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using dxmt::test::ComPtr;
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

TEST_F(DeviceControlSpec, StablePowerStateDisableIsIdempotentAndNonMutating) {
  EXPECT_EQ(context_.device()->SetStablePowerState(FALSE), S_OK);
  EXPECT_EQ(context_.device()->SetStablePowerState(FALSE), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
  ExpectCopyExecution();
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

} // namespace
