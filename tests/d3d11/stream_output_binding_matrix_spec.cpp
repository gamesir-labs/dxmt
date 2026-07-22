#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 SOSetTargets / SOGetTargets binding-state coverage. Two disjoint
// 64-buffer pools form 4096 unique primary target pairs, while two additional
// slots exercise null transitions and complete unbinding.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kStreamOutputBindingCaseCount = 4096;
constexpr std::uint32_t kPrimaryPoolSize = 64;
constexpr std::uint32_t kBufferCount = 2 * kPrimaryPoolSize + 2;
constexpr std::uint32_t kTargetCount = 4;

const dxmt::test::LogicalCaseFamilyRegistration kStreamOutputBindingCases(
    "D3D11StreamOutputBindingMatrixSpec."
    "RoundTrips4096TargetCombinationsAndClearsState",
    "D3D11.SOGetTargets.BindingState.", kStreamOutputBindingCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "SOSetTargets,SOGetTargets,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "two disjoint 64-buffer primary pools and two optional stream-output "
     "buffers, all owned by one test-local device",
     "bind each selected unique primary pair plus optional targets, read all "
     "four slots, release getter references, and unbind every target",
     "SOGetTargets returns the exact bound COM objects and all four slots are "
     "null after SOSetTargets with zero buffers",
     "logical ID, selected-case count, expected and actual target addresses, "
     "buffer indexes, byte offsets, failing slot and phase, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kStreamOutputBindingCost("D3D11StreamOutputBindingMatrixSpec."
                             "RoundTrips4096TargetCombinationsAndClearsState",
                             dxmt::test::kResourceTestCost);

struct BindingCase {
  std::array<std::uint32_t, 2> primary_indexes;
  std::array<bool, 2> optional_bound;
  std::array<UINT, kTargetCount> offsets;
};

BindingCase MakeBindingCase(std::uint32_t logical) {
  return {{{logical & 63u, kPrimaryPoolSize + ((logical >> 6u) & 63u)}},
          {{(logical & 1u) != 0, (logical & 2u) != 0}},
          {{((logical >> 2u) & 3u) * 4u, ((logical >> 4u) & 3u) * 4u,
            ((logical >> 8u) & 3u) * 4u, ((logical >> 10u) & 3u) * 4u}}};
}

class D3D11StreamOutputBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11StreamOutputBindingMatrixSpec,
       RoundTrips4096TargetCombinationsAndClearsState) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kStreamOutputBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kStreamOutputBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kStreamOutputBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 64;
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
  std::vector<ComPtr<ID3D11Buffer>> buffers(kBufferCount);
  for (std::uint32_t index = 0; index < kBufferCount; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "buffer_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kStreamOutputBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const BindingCase binding = MakeBindingCase(logical);
    std::array<ID3D11Buffer *, kTargetCount> expected = {
        buffers[binding.primary_indexes[0]].get(),
        buffers[binding.primary_indexes[1]].get(),
        binding.optional_bound[0] ? buffers[2 * kPrimaryPoolSize].get()
                                  : nullptr,
        binding.optional_bound[1] ? buffers[2 * kPrimaryPoolSize + 1].get()
                                  : nullptr,
    };
    context_.context()->SOSetTargets(kTargetCount, expected.data(),
                                     binding.offsets.data());

    std::array<ID3D11Buffer *, kTargetCount> actual = {};
    context_.context()->SOGetTargets(kTargetCount, actual.data());
    std::array<const void *, kTargetCount> actual_addresses = {};
    std::uint32_t failing_slot = kTargetCount;
    for (std::uint32_t slot = 0; slot < kTargetCount; ++slot) {
      actual_addresses[slot] = actual[slot];
      if (actual[slot] != expected[slot] && failing_slot == kTargetCount)
        failing_slot = slot;
      if (actual[slot])
        actual[slot]->Release();
    }

    context_.context()->SOSetTargets(0, nullptr, nullptr);
    std::array<ID3D11Buffer *, kTargetCount> after_unbind = {};
    context_.context()->SOGetTargets(kTargetCount, after_unbind.data());
    std::uint32_t stale_slot = kTargetCount;
    for (std::uint32_t slot = 0; slot < kTargetCount; ++slot) {
      if (after_unbind[slot] && stale_slot == kTargetCount)
        stale_slot = slot;
      if (after_unbind[slot])
        after_unbind[slot]->Release();
    }

    if (failing_slot == kTargetCount && stale_slot == kTargetCount)
      continue;

    const bool failed_during_get = failing_slot != kTargetCount;
    const std::uint32_t slot = failed_during_get ? failing_slot : stale_slot;
    const auto case_id =
        dxmt::test::LogicalCaseId(kStreamOutputBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kStreamOutputBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 queue=Immediate "
           "capability=SOSetTargets,SOGetTargets,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kStreamOutputBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical << " primary_indexes=("
        << binding.primary_indexes[0] << ',' << binding.primary_indexes[1]
        << ") optional_bound=(" << binding.optional_bound[0] << ','
        << binding.optional_bound[1] << ") offsets=(" << binding.offsets[0]
        << ',' << binding.offsets[1] << ',' << binding.offsets[2] << ','
        << binding.offsets[3] << ") selected_cases=" << selected_cases.size()
        << " failing_slot=" << slot
        << " phase=" << (failed_during_get ? "get" : "unbind") << '\n'
        << "ExpectedAddresses: (" << expected[0] << ',' << expected[1] << ','
        << expected[2] << ',' << expected[3] << ") actual=("
        << actual_addresses[0] << ',' << actual_addresses[1] << ','
        << actual_addresses[2] << ',' << actual_addresses[3] << ")\n"
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
  context_.context()->SOSetTargets(0, nullptr, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
