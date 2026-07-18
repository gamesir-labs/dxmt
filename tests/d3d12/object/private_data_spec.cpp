#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using dxmt::test::ComPtr;
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

  void ExpectObjectRemainsUsable() {
    const GUID key = {0xa7ff7cd1,
                      0xd276,
                      0x47e8,
                      {0xaf, 0xd7, 0x68, 0x41, 0xf0, 0xe8, 0x97, 0x3a}};
    constexpr std::uint32_t expected = 0xc001d00du;
    ASSERT_EQ(object()->SetPrivateData(key, sizeof(expected), &expected), S_OK);
    std::uint32_t actual = 0;
    UINT size = sizeof(actual);
    ASSERT_EQ(object()->GetPrivateData(key, &size, &actual), S_OK);
    EXPECT_EQ(size, sizeof(actual));
    EXPECT_EQ(actual, expected);
  }

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

TEST_F(PrivateDataSpec, SetNameAcceptsNullAndEmptyNames) {
  EXPECT_TRUE(SUCCEEDED(object()->SetName(nullptr)));
  EXPECT_EQ(object()->SetName(L""), S_OK);
  ExpectObjectRemainsUsable();
}

TEST_F(PrivateDataSpec, SetNameAcceptsUnicode) {
  EXPECT_EQ(object()->SetName(L"DXMT 队列 Δ U0001f680"), S_OK);
  ExpectObjectRemainsUsable();
}

TEST_F(PrivateDataSpec, SetNameAcceptsLongNames) {
  const std::wstring name(8192, L'x');
  EXPECT_EQ(object()->SetName(name.c_str()), S_OK);
  ExpectObjectRemainsUsable();
}

TEST_F(PrivateDataSpec, SetNameCanBeReplacedRepeatedly) {
  for (const wchar_t *name : {L"first", L"second", L"third", L""})
    ASSERT_EQ(object()->SetName(name), S_OK);
  ExpectObjectRemainsUsable();
}

TEST_F(PrivateDataSpec, CommandObjectsKeepMetadataAndInterfacesIsolated) {
  D3D12_INDIRECT_ARGUMENT_DESC argument = {};
  argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
  signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  signature_desc.NumArgumentDescs = 1;
  signature_desc.pArgumentDescs = &argument;
  ComPtr<ID3D12CommandSignature> signature;
  ASSERT_EQ(context_.device()->CreateCommandSignature(
                &signature_desc, nullptr, __uuidof(ID3D12CommandSignature),
                reinterpret_cast<void **>(signature.put())),
            S_OK);
  ASSERT_TRUE(signature);

  struct CommandObjectCase {
    const wchar_t *name;
    ID3D12Object *object;
    std::uint32_t value;
  };
  const std::array objects = {
      CommandObjectCase{L"allocator metadata", context_.allocator(),
                        0xa110ca7eu},
      CommandObjectCase{L"command list metadata", context_.list(),
                        0xc0111570u},
      CommandObjectCase{L"signature metadata", signature.get(),
                        0x519a7e00u},
  };
  const GUID value_key = {0xbff8d24f,
                          0x479b,
                          0x44fe,
                          {0xb7, 0xad, 0x47, 0x77, 0x73, 0xce, 0xb8, 0xa1}};
  const GUID interface_key = {
      0xe57f9866,
      0x4828,
      0x4832,
      {0x86, 0x74, 0x81, 0xda, 0x5a, 0xbe, 0xc2, 0xd9}};

  for (const auto &test : objects) {
    SCOPED_TRACE(test.value);
    ASSERT_EQ(test.object->SetName(test.name), S_OK);
    ASSERT_EQ(test.object->SetPrivateData(value_key, sizeof(test.value),
                                          &test.value),
              S_OK);
    ASSERT_EQ(test.object->SetPrivateDataInterface(interface_key,
                                                   context_.device()),
              S_OK);
  }

  for (const auto &test : objects) {
    SCOPED_TRACE(test.value);
    std::uint32_t actual = 0;
    UINT value_size = sizeof(actual);
    ASSERT_EQ(test.object->GetPrivateData(value_key, &value_size, &actual),
              S_OK);
    EXPECT_EQ(value_size, sizeof(actual));
    EXPECT_EQ(actual, test.value);

    IUnknown *stored_interface = nullptr;
    UINT interface_size = sizeof(stored_interface);
    ASSERT_EQ(test.object->GetPrivateData(interface_key, &interface_size,
                                          &stored_interface),
              S_OK);
    ASSERT_NE(stored_interface, nullptr);
    EXPECT_EQ(interface_size, sizeof(stored_interface));
    ComPtr<IUnknown> stored_identity;
    ComPtr<IUnknown> device_identity;
    EXPECT_EQ(stored_interface->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(stored_identity.put())),
              S_OK);
    EXPECT_EQ(context_.device()->QueryInterface(
                  __uuidof(IUnknown),
                  reinterpret_cast<void **>(device_identity.put())),
              S_OK);
    EXPECT_EQ(stored_identity.get(), device_identity.get());
    stored_interface->Release();

    EXPECT_EQ(test.object->SetPrivateData(value_key, 0, nullptr), S_OK);
    EXPECT_EQ(test.object->SetPrivateDataInterface(interface_key, nullptr),
              S_OK);
  }
}

} // namespace
