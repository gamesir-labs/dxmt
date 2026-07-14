#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace {

using dxmt::test::D3D12TestContext;

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
    const ULONG references =
        ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!references)
      delete this;
    return references;
  }

private:
  std::atomic_ulong ref_count_{1};
  std::shared_ptr<std::atomic_bool> destroyed_;
};

class PrivateDataSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  ID3D12Object *object() const { return context_.queue(); }

  D3D12TestContext context_;
};

TEST_F(PrivateDataSpec, StoresByteArraysAtRepresentativeSizes) {
  for (UINT size : {1u, 16u, 4096u}) {
    std::vector<std::uint8_t> expected(size);
    for (UINT index = 0; index < size; ++index)
      expected[index] = static_cast<std::uint8_t>(index * 37u + size);
    const GUID key = {
        size, 0x70bd, 0x42a1, {0x87, 0x45, 0x11, 0x32, 0, 0, 0, 1}};
    ASSERT_TRUE(
        SUCCEEDED(object()->SetPrivateData(key, size, expected.data())));
    std::vector<std::uint8_t> actual(size);
    UINT actual_size = size;

    ASSERT_TRUE(
        SUCCEEDED(object()->GetPrivateData(key, &actual_size, actual.data())));
    EXPECT_EQ(actual_size, size);
    EXPECT_EQ(actual, expected);
  }
}

TEST_F(PrivateDataSpec, ReportsRequiredSizeWithoutDestination) {
  constexpr std::array<std::uint8_t, 5> data = {1, 3, 5, 7, 9};
  const GUID key = __uuidof(ID3D12Fence);
  ASSERT_TRUE(
      SUCCEEDED(object()->SetPrivateData(key, data.size(), data.data())));
  UINT size = 0;

  EXPECT_EQ(object()->GetPrivateData(key, &size, nullptr), S_OK);
  EXPECT_EQ(size, data.size());
}

TEST_F(PrivateDataSpec, StoresEmptyDataAsAnExistingEntry) {
  const GUID key = __uuidof(ID3D12CommandAllocator);
  const std::uint8_t marker = 0;
  ASSERT_TRUE(SUCCEEDED(object()->SetPrivateData(key, 0, &marker)));
  UINT size = 1;

  EXPECT_EQ(object()->GetPrivateData(key, &size, nullptr), S_OK);
  EXPECT_EQ(size, 0u);
}

TEST_F(PrivateDataSpec, NullSizePointerIsRejected) {
  const GUID key = __uuidof(ID3D12CommandList);

  EXPECT_EQ(object()->GetPrivateData(key, nullptr, nullptr), E_INVALIDARG);
}

TEST_F(PrivateDataSpec, SmallDestinationReportsRequiredSizeWithoutWriting) {
  constexpr std::array<std::uint8_t, 5> data = {2, 4, 6, 8, 10};
  const GUID key = __uuidof(ID3D12Resource);
  ASSERT_TRUE(
      SUCCEEDED(object()->SetPrivateData(key, data.size(), data.data())));
  std::array<std::uint8_t, 4> output = {0xcc, 0xcc, 0xcc, 0xcc};
  UINT size = output.size();

  EXPECT_EQ(object()->GetPrivateData(key, &size, output.data()),
            DXGI_ERROR_MORE_DATA);
  EXPECT_EQ(size, data.size());
  EXPECT_EQ(output, (std::array<std::uint8_t, 4>{0xcc, 0xcc, 0xcc, 0xcc}));
}

TEST_F(PrivateDataSpec, ReplacingDataUpdatesSizeAndContents) {
  constexpr std::array<std::uint8_t, 2> initial = {1, 2};
  constexpr std::array<std::uint8_t, 4> replacement = {9, 8, 7, 6};
  const GUID key = __uuidof(ID3D12Heap);
  ASSERT_TRUE(
      SUCCEEDED(object()->SetPrivateData(key, initial.size(), initial.data())));
  ASSERT_TRUE(SUCCEEDED(
      object()->SetPrivateData(key, replacement.size(), replacement.data())));
  std::array<std::uint8_t, 4> actual = {};
  UINT size = actual.size();

  ASSERT_TRUE(SUCCEEDED(object()->GetPrivateData(key, &size, actual.data())));
  EXPECT_EQ(size, replacement.size());
  EXPECT_EQ(actual, replacement);
}

TEST_F(PrivateDataSpec, DifferentGuidsRemainIndependent) {
  const GUID first_key = __uuidof(ID3D12Fence);
  const GUID second_key = __uuidof(ID3D12Resource);
  const std::uint32_t first = 0x12345678;
  const std::uint32_t second = 0xabcdef01;
  ASSERT_TRUE(
      SUCCEEDED(object()->SetPrivateData(first_key, sizeof(first), &first)));
  ASSERT_TRUE(
      SUCCEEDED(object()->SetPrivateData(second_key, sizeof(second), &second)));

  for (const auto &[key, expected] :
       {std::pair{first_key, first}, std::pair{second_key, second}}) {
    std::uint32_t actual = 0;
    UINT size = sizeof(actual);
    ASSERT_TRUE(SUCCEEDED(object()->GetPrivateData(key, &size, &actual)));
    EXPECT_EQ(actual, expected);
  }
}

TEST_F(PrivateDataSpec, NullDataDeletesExistingEntry) {
  const GUID key = __uuidof(ID3D12CommandQueue);
  const std::uint32_t value = 42;
  ASSERT_TRUE(SUCCEEDED(object()->SetPrivateData(key, sizeof(value), &value)));
  ASSERT_EQ(object()->SetPrivateData(key, 0, nullptr), S_OK);
  UINT size = sizeof(value);
  std::uint32_t output = 0;

  EXPECT_EQ(object()->GetPrivateData(key, &size, &output),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(size, 0u);
}

TEST_F(PrivateDataSpec, DeletingMissingEntryReturnsFalse) {
  const GUID key = __uuidof(ID3D12GraphicsCommandList);

  EXPECT_EQ(object()->SetPrivateData(key, 0, nullptr), S_FALSE);
}

TEST_F(PrivateDataSpec, InterfaceIsRetainedAndReturnedWithReference) {
  const GUID key = __uuidof(ID3D12DeviceChild);
  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(SUCCEEDED(object()->SetPrivateDataInterface(key, probe)));
  probe->Release();
  ASSERT_FALSE(destroyed->load(std::memory_order_acquire));
  IUnknown *retrieved = nullptr;
  UINT size = sizeof(retrieved);

  ASSERT_TRUE(SUCCEEDED(object()->GetPrivateData(key, &size, &retrieved)));
  EXPECT_EQ(size, sizeof(retrieved));
  EXPECT_EQ(retrieved, probe);
  retrieved->Release();
  EXPECT_FALSE(destroyed->load(std::memory_order_acquire));

  EXPECT_EQ(object()->SetPrivateDataInterface(key, nullptr), S_OK);
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

TEST_F(PrivateDataSpec, ReplacingInterfaceReleasesPreviousReference) {
  const GUID key = __uuidof(ID3D12Pageable);
  auto first_destroyed = std::make_shared<std::atomic_bool>(false);
  auto second_destroyed = std::make_shared<std::atomic_bool>(false);
  auto *first = new LifetimeProbe(first_destroyed);
  auto *second = new LifetimeProbe(second_destroyed);
  ASSERT_TRUE(SUCCEEDED(object()->SetPrivateDataInterface(key, first)));
  first->Release();
  ASSERT_TRUE(SUCCEEDED(object()->SetPrivateDataInterface(key, second)));
  second->Release();

  EXPECT_TRUE(first_destroyed->load(std::memory_order_acquire));
  EXPECT_FALSE(second_destroyed->load(std::memory_order_acquire));

  EXPECT_EQ(object()->SetPrivateDataInterface(key, nullptr), S_OK);
  EXPECT_TRUE(second_destroyed->load(std::memory_order_acquire));
}

TEST_F(PrivateDataSpec, ObjectDestructionReleasesPrivateInterface) {
  dxmt::test::ComPtr<ID3D12Fence> fence;
  ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
      0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))));
  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  ASSERT_TRUE(
      SUCCEEDED(fence->SetPrivateDataInterface(__uuidof(IUnknown), probe)));
  probe->Release();
  ASSERT_FALSE(destroyed->load(std::memory_order_acquire));

  fence.reset();
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

} // namespace
