#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public D3D11 performance-counter coverage. Capability discovery and invalid
// input checks are parallel-safe. Counter creation is serial because native
// drivers may expose the counter bank as a global non-exclusive resource.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

class D3D11CounterContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11_COUNTER_INFO CounterInfo() const {
    D3D11_COUNTER_INFO info = {};
    context_.device()->CheckCounterInfo(&info);
    return info;
  }

  D3D11TestContext context_;
};

TEST_F(D3D11CounterContractSpec, ReportsCoherentCapabilityInfo) {
  D3D11_COUNTER_INFO info = {
      static_cast<D3D11_COUNTER>(std::numeric_limits<std::uint32_t>::max()),
      std::numeric_limits<UINT>::max(),
      std::numeric_limits<UINT8>::max(),
  };
  context_.device()->CheckCounterInfo(&info);

  const auto last_counter =
      static_cast<std::uint32_t>(info.LastDeviceDependentCounter);
  EXPECT_TRUE(last_counter == 0 ||
              last_counter >=
                  static_cast<std::uint32_t>(D3D11_COUNTER_DEVICE_DEPENDENT_0));
  EXPECT_LE(info.NumDetectableParallelUnits, 4);
  if (last_counter != 0) {
    EXPECT_GT(info.NumSimultaneousCounters, 0u);
    EXPECT_GT(info.NumDetectableParallelUnits, 0u);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11CounterContractSpec, RejectsNullDescriptions) {
  ID3D11Counter *counter =
      reinterpret_cast<ID3D11Counter *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateCounter(nullptr, &counter), E_INVALIDARG);
  EXPECT_EQ(counter, nullptr);

  D3D11_COUNTER_TYPE type = D3D11_COUNTER_TYPE_UINT16;
  UINT active_counters = 17;
  EXPECT_EQ(context_.device()->CheckCounter(nullptr, &type, &active_counters,
                                            nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr),
            E_INVALIDARG);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11CounterContractSpec, RejectsOutOfRangeDeviceCounters) {
  const D3D11_COUNTER_INFO info = CounterInfo();
  const auto last_counter =
      static_cast<std::uint32_t>(info.LastDeviceDependentCounter);
  const auto first_counter =
      static_cast<std::uint32_t>(D3D11_COUNTER_DEVICE_DEPENDENT_0);
  const std::array<D3D11_COUNTER, 2> invalid_counters = {
      static_cast<D3D11_COUNTER>(first_counter - 1),
      static_cast<D3D11_COUNTER>(last_counter == 0 ? first_counter
                                                   : last_counter + 1),
  };

  for (const D3D11_COUNTER invalid_counter : invalid_counters) {
    const D3D11_COUNTER_DESC desc = {invalid_counter, 0};
    ID3D11Counter *counter =
        reinterpret_cast<ID3D11Counter *>(static_cast<std::uintptr_t>(1));
    EXPECT_EQ(context_.device()->CreateCounter(&desc, &counter), E_INVALIDARG);
    EXPECT_EQ(counter, nullptr);

    D3D11_COUNTER_TYPE type = D3D11_COUNTER_TYPE_UINT16;
    UINT active_counters = 17;
    EXPECT_EQ(context_.device()->CheckCounter(&desc, &type, &active_counters,
                                              nullptr, nullptr, nullptr,
                                              nullptr, nullptr, nullptr),
              E_INVALIDARG);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11CounterContractSpec, DescribesLastReportedCounter) {
  const D3D11_COUNTER_INFO info = CounterInfo();
  if (static_cast<std::uint32_t>(info.LastDeviceDependentCounter) == 0) {
    EXPECT_EQ(info.NumSimultaneousCounters, 0u);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return;
  }

  const D3D11_COUNTER_DESC desc = {info.LastDeviceDependentCounter, 0};
  D3D11_COUNTER_TYPE type = D3D11_COUNTER_TYPE_FLOAT32;
  UINT active_counters = 0;
  UINT name_length = 0;
  UINT units_length = 0;
  UINT description_length = 0;
  ASSERT_EQ(context_.device()->CheckCounter(
                &desc, &type, &active_counters, nullptr, &name_length, nullptr,
                &units_length, nullptr, &description_length),
            S_OK);

  EXPECT_GE(type, D3D11_COUNTER_TYPE_FLOAT32);
  EXPECT_LE(type, D3D11_COUNTER_TYPE_UINT64);
  EXPECT_GT(active_counters, 0u);
  EXPECT_LE(active_counters, info.NumSimultaneousCounters);
  ASSERT_GT(name_length, 1u);
  ASSERT_GT(units_length, 1u);
  ASSERT_GT(description_length, 1u);

  std::vector<char> name(name_length);
  std::vector<char> units(units_length);
  std::vector<char> description(description_length);
  ASSERT_EQ(context_.device()->CheckCounter(
                &desc, &type, &active_counters, name.data(), &name_length,
                units.data(), &units_length, description.data(),
                &description_length),
            S_OK);
  EXPECT_EQ(name.back(), '\0');
  EXPECT_EQ(units.back(), '\0');
  EXPECT_EQ(description.back(), '\0');
  EXPECT_NE(name.front(), '\0');
  EXPECT_NE(units.front(), '\0');
  EXPECT_NE(description.front(), '\0');
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

DXMT_SERIAL_TEST_F(D3D11CounterContractSpec,
                   CreatesLastReportedCounterWithPublicComContracts) {
  const D3D11_COUNTER_INFO info = CounterInfo();
  if (static_cast<std::uint32_t>(info.LastDeviceDependentCounter) == 0) {
    EXPECT_EQ(info.NumSimultaneousCounters, 0u);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return;
  }

  const D3D11_COUNTER_DESC expected_desc = {
      info.LastDeviceDependentCounter,
      0,
  };
  D3D11_COUNTER_TYPE type = D3D11_COUNTER_TYPE_FLOAT32;
  UINT active_counters = 0;
  ASSERT_EQ(context_.device()->CheckCounter(&expected_desc, &type,
                                            &active_counters, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr),
            S_OK);

  ComPtr<ID3D11Counter> counter;
  ASSERT_EQ(context_.device()->CreateCounter(&expected_desc, counter.put()),
            S_OK);
  ASSERT_NE(counter.get(), nullptr);

  D3D11_COUNTER_DESC observed_desc = {};
  counter->GetDesc(&observed_desc);
  EXPECT_EQ(observed_desc.Counter, expected_desc.Counter);
  EXPECT_EQ(observed_desc.MiscFlags, expected_desc.MiscFlags);

  UINT expected_data_size = 0;
  switch (type) {
  case D3D11_COUNTER_TYPE_FLOAT32:
  case D3D11_COUNTER_TYPE_UINT32:
    expected_data_size = 4;
    break;
  case D3D11_COUNTER_TYPE_UINT16:
    expected_data_size = 2;
    break;
  case D3D11_COUNTER_TYPE_UINT64:
    expected_data_size = 8;
    break;
  }
  EXPECT_EQ(counter->GetDataSize(), expected_data_size);

  ComPtr<ID3D11Device> owner;
  counter->GetDevice(owner.put());
  EXPECT_EQ(owner.get(), context_.device());

  ComPtr<ID3D11DeviceChild> child;
  ASSERT_EQ(counter->QueryInterface(__uuidof(ID3D11DeviceChild),
                                    reinterpret_cast<void **>(child.put())),
            S_OK);
  ComPtr<IUnknown> counter_identity;
  ComPtr<IUnknown> child_identity;
  ASSERT_EQ(counter->QueryInterface(
                __uuidof(IUnknown),
                reinterpret_cast<void **>(counter_identity.put())),
            S_OK);
  ASSERT_EQ(
      child->QueryInterface(__uuidof(IUnknown),
                            reinterpret_cast<void **>(child_identity.put())),
      S_OK);
  EXPECT_EQ(counter_identity.get(), child_identity.get());
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
