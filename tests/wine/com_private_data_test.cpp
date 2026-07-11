#include <dxmt_test.hpp>

#include "com/com_guid.hpp"
#include "com/com_private_data.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

constexpr GUID kDataGuid =
    dxmt::guid::make_guid("00112233-4455-6677-8899-aabbccddeeff");
constexpr GUID kInterfaceGuid =
    dxmt::guid::make_guid("10213243-5465-7687-98a9-bacbdcedfe0f");
constexpr GUID kDebugObjectNameWGuid =
    dxmt::guid::make_guid("4cca5fd8-921f-42c8-8566-70caf2a9b741");

class CountingUnknown final : public IUnknown {
public:
  HRESULT QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_INVALIDARG;
    *object = nullptr;
    if (iid != IID_IUnknown)
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG AddRef() override { return ++references_; }
  ULONG Release() override { return --references_; }
  ULONG references() const { return references_; }

private:
  ULONG references_ = 1;
};

struct TransferState {
  ULONG references = 1;
  bool reached_zero = false;
  bool resurrected = false;
};

class TransferUnknown final : public IUnknown {
public:
  explicit TransferUnknown(TransferState &state) : state_(state) {}

  HRESULT QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_INVALIDARG;
    if (iid != IID_IUnknown) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG AddRef() override {
    if (state_.reached_zero)
      state_.resurrected = true;
    return ++state_.references;
  }

  ULONG Release() override {
    const ULONG references = --state_.references;
    if (!references)
      state_.reached_zero = true;
    return references;
  }

private:
  TransferState &state_;
};

} // namespace

TEST(ComGuid, ParsesCanonicalTextAndFormatsLowercase) {
  static_assert(kDataGuid.Data1 == 0x00112233);
  static_assert(kDataGuid.Data2 == 0x4455);
  static_assert(kDataGuid.Data3 == 0x6677);
  static_assert(kDataGuid.Data4[0] == 0x88);
  static_assert(kDataGuid.Data4[7] == 0xff);

  std::ostringstream output;
  output << kDataGuid;
  EXPECT_EQ(output.str(), "00112233-4455-6677-8899-aabbccddeeff");
}

TEST(ComGuid, RejectsMalformedDigitsAndSeparators) {
  EXPECT_THROW(
      dxmt::guid::make_guid_helper("00112233-4455-6677-8899-aabbccddeefg"),
      std::domain_error);
  EXPECT_THROW(
      dxmt::guid::make_guid_helper("00112233_4455-6677-8899-aabbccddeeff"),
      std::domain_error);
}

TEST(ComPrivateData, StoresReplacesAndRemovesBytePayloads) {
  dxmt::ComPrivateData storage;
  UINT size = 99;
  EXPECT_EQ(storage.getData(kDataGuid, nullptr, nullptr), E_INVALIDARG);
  EXPECT_EQ(storage.getData(kDataGuid, &size, nullptr), DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(size, 0u);

  constexpr std::array<uint8_t, 4> original = {1, 3, 5, 7};
  ASSERT_EQ(storage.setData(kDataGuid, original.size(), original.data()), S_OK);

  EXPECT_EQ(storage.getData(kDataGuid, &size, nullptr), S_OK);
  EXPECT_EQ(size, original.size());

  std::array<uint8_t, 4> output = {};
  size = 2;
  EXPECT_EQ(storage.getData(kDataGuid, &size, output.data()),
            DXGI_ERROR_MORE_DATA);
  EXPECT_EQ(size, original.size());
  EXPECT_EQ(output, (std::array<uint8_t, 4>{}));

  size = output.size();
  EXPECT_EQ(storage.getData(kDataGuid, &size, output.data()), S_OK);
  EXPECT_EQ(output, original);

  constexpr std::array<uint8_t, 2> replacement = {11, 13};
  EXPECT_EQ(storage.setData(kDataGuid, replacement.size(), replacement.data()),
            S_OK);
  size = output.size();
  EXPECT_EQ(storage.getData(kDataGuid, &size, output.data()), S_OK);
  EXPECT_EQ(size, replacement.size());
  EXPECT_TRUE(
      std::equal(replacement.begin(), replacement.end(), output.begin()));

  EXPECT_EQ(storage.setData(kDataGuid, 0, nullptr), S_OK);
  EXPECT_EQ(storage.setData(kDataGuid, 0, nullptr), S_FALSE);
}

TEST(ComPrivateData, StoresNullTerminatedDebugName) {
  dxmt::ComPrivateData storage;
  constexpr std::array<WCHAR, 5> name = {L'D', L'X', L'M', L'T', L'\0'};
  ASSERT_EQ(storage.setName(name.data()), S_OK);

  std::array<WCHAR, 5> output = {};
  UINT size = sizeof(output);
  EXPECT_EQ(storage.getData(kDebugObjectNameWGuid, &size, output.data()), S_OK);
  EXPECT_EQ(size, sizeof(name));
  EXPECT_EQ(output, name);

  EXPECT_EQ(storage.setName(nullptr), S_OK);
  EXPECT_EQ(storage.getData(kDebugObjectNameWGuid, &size, nullptr),
            DXGI_ERROR_NOT_FOUND);
}

TEST(ComPrivateData, BalancesInterfaceReferencesAndNullRemovesEntry) {
  CountingUnknown first;
  CountingUnknown second;

  {
    dxmt::ComPrivateData storage;
    EXPECT_EQ(storage.setInterface(kInterfaceGuid, &first), S_OK);
    EXPECT_EQ(first.references(), 2u);

    UINT size = 0;
    EXPECT_EQ(storage.getData(kInterfaceGuid, &size, nullptr), S_OK);
    EXPECT_EQ(size, sizeof(IUnknown *));

    IUnknown *output = nullptr;
    EXPECT_EQ(storage.getData(kInterfaceGuid, &size, &output), S_OK);
    EXPECT_EQ(output, &first);
    EXPECT_EQ(first.references(), 3u);
    output->Release();

    EXPECT_EQ(storage.setInterface(kInterfaceGuid, &second), S_OK);
    EXPECT_EQ(first.references(), 1u);
    EXPECT_EQ(second.references(), 2u);

    EXPECT_EQ(storage.setInterface(kInterfaceGuid, nullptr), S_OK);
    EXPECT_EQ(second.references(), 1u);
    EXPECT_EQ(storage.getData(kInterfaceGuid, &size, nullptr),
              DXGI_ERROR_NOT_FOUND);
  }

  EXPECT_EQ(first.references(), 1u);
  EXPECT_EQ(second.references(), 1u);
}

TEST(ComPointer, SameRawPointerAndSelfMoveKeepTheOwnedReferenceAlive) {
  TransferState state;
  TransferUnknown object(state);
  {
    auto pointer = dxmt::Com<IUnknown>::transfer(&object);
    pointer = pointer.ptr();
    EXPECT_FALSE(state.reached_zero);
    EXPECT_FALSE(state.resurrected);
    EXPECT_EQ(state.references, 1u);

    auto &alias = pointer;
    pointer = std::move(alias);
    EXPECT_EQ(pointer.ptr(), &object);
    EXPECT_EQ(state.references, 1u);
  }
  EXPECT_TRUE(state.reached_zero);
  EXPECT_FALSE(state.resurrected);
  EXPECT_EQ(state.references, 0u);
}
