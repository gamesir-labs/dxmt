#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 CSSetUnorderedAccessViews / CSGetUnorderedAccessViews binding
// state coverage. Two disjoint 64-view pools form 4096 unique pairs, and each
// pair is also placed at a derived legal start slot to exercise ranged getters.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kComputeUavBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kViewCount = 2 * kPrimaryPoolSize;
constexpr UINT kBoundViewCount = 2;
constexpr UINT kGuaranteedUavSlotCount = D3D11_PS_CS_UAV_REGISTER_COUNT;
constexpr UINT kStartSlotCount = kGuaranteedUavSlotCount - kBoundViewCount + 1u;

const dxmt::test::LogicalCaseFamilyRegistration kComputeUavBindingCases(
    "D3D11ComputeUavBindingMatrixSpec."
    "RoundTrips4096ViewPairsAcrossSlotsAndClearsState",
    "D3D11.CSGetUnorderedAccessViews.Binding.", kComputeUavBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "Immediate",
      "CSSetUnorderedAccessViews,CSGetUnorderedAccessViews,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint pools of sixty-four test-local structured buffers and UAVs",
     "bind every selected unique UAV pair at a derived legal start slot, query "
     "the exact range and every guaranteed compute UAV slot, release every "
     "getter reference, then unbind the pair",
     "both getters return the exact COM objects only in the requested bound "
     "slots and all guaranteed slots are null after unbinding",
     "logical ID, selected-case count, pool indexes, start slot, expected and "
     "actual view addresses, failing phase and slot, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kComputeUavBindingCost("D3D11ComputeUavBindingMatrixSpec."
                           "RoundTrips4096ViewPairsAcrossSlotsAndClearsState",
                           dxmt::test::kResourceTestCost);

struct UavBindingCase {
  std::array<std::uint32_t, kBoundViewCount> view_indexes;
  UINT start_slot;
};

UavBindingCase BindingForCase(std::uint32_t logical) {
  const std::uint32_t first = logical & 63u;
  const std::uint32_t second = (logical >> 6u) & 63u;
  return {{{first, kPrimaryPoolSize + second}},
          static_cast<UINT>((first ^ second) % kStartSlotCount)};
}

class D3D11ComputeUavBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ComputeUavBindingMatrixSpec,
       RoundTrips4096ViewPairsAcrossSlotsAndClearsState) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kComputeUavBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kComputeUavBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kComputeUavBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 4u * sizeof(std::uint32_t);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  buffer_desc.StructureByteStride = sizeof(std::uint32_t);
  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 4;

  std::vector<ComPtr<ID3D11Buffer>> buffers(kViewCount);
  std::vector<ComPtr<ID3D11UnorderedAccessView>> views(kViewCount);
  for (std::uint32_t index = 0; index < kViewCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "buffer_index=" << index;
    ASSERT_EQ(context_.device()->CreateUnorderedAccessView(
                  buffers[index].get(), &uav_desc, views[index].put()),
              S_OK)
        << "view_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kComputeUavBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const UavBindingCase binding = BindingForCase(logical);
    std::array<ID3D11UnorderedAccessView *, kBoundViewCount> expected = {
        views[binding.view_indexes[0]].get(),
        views[binding.view_indexes[1]].get(),
    };
    context_.context()->CSSetUnorderedAccessViews(
        binding.start_slot, kBoundViewCount, expected.data(), nullptr);

    std::array<ID3D11UnorderedAccessView *, kBoundViewCount> ranged = {};
    context_.context()->CSGetUnorderedAccessViews(
        binding.start_slot, kBoundViewCount, ranged.data());
    std::array<const void *, kBoundViewCount> ranged_addresses = {};
    UINT ranged_failure = kBoundViewCount;
    for (UINT index = 0; index < kBoundViewCount; ++index) {
      ranged_addresses[index] = ranged[index];
      if (ranged[index] != expected[index] && ranged_failure == kBoundViewCount)
        ranged_failure = index;
      if (ranged[index])
        ranged[index]->Release();
    }

    std::array<ID3D11UnorderedAccessView *, kGuaranteedUavSlotCount> all = {};
    context_.context()->CSGetUnorderedAccessViews(0, kGuaranteedUavSlotCount,
                                                  all.data());
    std::array<const void *, kGuaranteedUavSlotCount> all_addresses = {};
    UINT all_failure = kGuaranteedUavSlotCount;
    for (UINT slot = 0; slot < kGuaranteedUavSlotCount; ++slot) {
      ID3D11UnorderedAccessView *expected_view = nullptr;
      if (slot >= binding.start_slot &&
          slot < binding.start_slot + kBoundViewCount)
        expected_view = expected[slot - binding.start_slot];
      all_addresses[slot] = all[slot];
      if (all[slot] != expected_view && all_failure == kGuaranteedUavSlotCount)
        all_failure = slot;
      if (all[slot])
        all[slot]->Release();
    }

    std::array<ID3D11UnorderedAccessView *, kBoundViewCount> null_views = {};
    context_.context()->CSSetUnorderedAccessViews(
        binding.start_slot, kBoundViewCount, null_views.data(), nullptr);
    std::array<ID3D11UnorderedAccessView *, kGuaranteedUavSlotCount>
        after_unbind = {};
    context_.context()->CSGetUnorderedAccessViews(0, kGuaranteedUavSlotCount,
                                                  after_unbind.data());
    std::array<const void *, kGuaranteedUavSlotCount> after_unbind_addresses =
        {};
    UINT stale_slot = kGuaranteedUavSlotCount;
    for (UINT slot = 0; slot < kGuaranteedUavSlotCount; ++slot) {
      after_unbind_addresses[slot] = after_unbind[slot];
      if (after_unbind[slot] && stale_slot == kGuaranteedUavSlotCount)
        stale_slot = slot;
      if (after_unbind[slot])
        after_unbind[slot]->Release();
    }

    if (ranged_failure == kBoundViewCount &&
        all_failure == kGuaranteedUavSlotCount &&
        stale_slot == kGuaranteedUavSlotCount)
      continue;

    const bool ranged_phase = ranged_failure != kBoundViewCount;
    const bool all_phase =
        !ranged_phase && all_failure != kGuaranteedUavSlotCount;
    const UINT failing_slot = ranged_phase
                                  ? binding.start_slot + ranged_failure
                                  : (all_phase ? all_failure : stale_slot);
    const UINT expected_index =
        failing_slot >= binding.start_slot &&
                failing_slot < binding.start_slot + kBoundViewCount
            ? failing_slot - binding.start_slot
            : kBoundViewCount;
    const void *expected_address =
        expected_index < kBoundViewCount ? expected[expected_index] : nullptr;
    const void *actual_address =
        ranged_phase ? ranged_addresses[ranged_failure]
                     : (all_phase ? all_addresses[all_failure]
                                  : after_unbind_addresses[stale_slot]);
    const auto case_id =
        dxmt::test::LogicalCaseId(kComputeUavBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kComputeUavBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=CSSetUnorderedAccessViews,CSGetUnorderedAccessViews,"
           "ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kComputeUavBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " view_indexes=("
        << binding.view_indexes[0] << ',' << binding.view_indexes[1]
        << ") start_slot=" << binding.start_slot
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: phase="
        << (ranged_phase ? "ranged_get"
                         : (all_phase ? "all_slots_get" : "unbind"))
        << " failing_slot=" << failing_slot
        << " expected_view=" << expected_address
        << " actual_view=" << actual_address << " ranged_actual=("
        << ranged_addresses[0] << ',' << ranged_addresses[1] << ")\n"
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11UnorderedAccessView *, kGuaranteedUavSlotCount> null_views =
      {};
  context_.context()->CSSetUnorderedAccessViews(0, kGuaranteedUavSlotCount,
                                                null_views.data(), nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
