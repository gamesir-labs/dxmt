#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 RSSetViewports / RSGetViewports state coverage. The logical
// matrix crosses every legal bound count, every legal getter capacity, and
// sixteen valid fractional viewport variants while checking count-only,
// zero-capacity, unused-slot, and complete-unbind behavior.

namespace {

using dxmt::test::D3D11TestContext;

constexpr UINT kMaxViewports =
    D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
constexpr std::uint32_t kViewportStateCaseCount =
    kMaxViewports * kMaxViewports * kMaxViewports;
static_assert(kViewportStateCaseCount == 4096);

const dxmt::test::LogicalCaseFamilyRegistration kViewportStateCases(
    "D3D11ViewportStateMatrixSpec."
    "RoundTrips4096CountCapacityAndViewportCombinations",
    "D3D11.RSGetViewports.State.", kViewportStateCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "Immediate", "RSSetViewports,RSGetViewports"},
     dxmt::test::kNormalTestCost,
     "one test-local immediate context and fixed arrays of at most sixteen "
     "D3D11 viewports",
     "atomically bind every selected count and valid fractional viewport "
     "variant, query the count alone and with every output capacity, then bind "
     "zero viewports",
     "the getter returns the exact count and viewport values, zero-fills "
     "requested unused slots, preserves slots beyond the requested capacity, "
     "and reports zero viewports after unbinding",
     "logical ID, selected-case count, bound count, requested capacity, "
     "variant, returned counts, failing phase and slot, expected and actual "
     "viewport, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kViewportStateCost("D3D11ViewportStateMatrixSpec."
                       "RoundTrips4096CountCapacityAndViewportCombinations",
                       dxmt::test::kNormalTestCost);

struct ViewportStateCase {
  UINT bound_count;
  UINT requested_capacity;
  UINT variant;
};

ViewportStateCase StateForCase(std::uint32_t logical) {
  return {1u + logical % kMaxViewports,
          1u + (logical / kMaxViewports) % kMaxViewports,
          logical / (kMaxViewports * kMaxViewports)};
}

D3D11_VIEWPORT ViewportForCase(UINT variant, UINT slot) {
  const FLOAT fraction = static_cast<FLOAT>(variant) * 0.25f;
  return {-16.0f + static_cast<FLOAT>(slot) * 8.0f + fraction,
          -8.0f + static_cast<FLOAT>(slot) * 4.0f + fraction * 0.5f,
          32.0f + static_cast<FLOAT>(slot) + fraction,
          16.0f + static_cast<FLOAT>(slot) * 0.5f + fraction,
          static_cast<FLOAT>(variant) / 32.0f,
          0.5f + static_cast<FLOAT>(variant) / 32.0f};
}

constexpr D3D11_VIEWPORT ZeroViewport() { return {0, 0, 0, 0, 0, 0}; }

constexpr D3D11_VIEWPORT PoisonViewport() {
  return {100001, 100002, 100003, 100004, 100005, 100006};
}

bool ViewportEquals(const D3D11_VIEWPORT &left, const D3D11_VIEWPORT &right) {
  return left.TopLeftX == right.TopLeftX && left.TopLeftY == right.TopLeftY &&
         left.Width == right.Width && left.Height == right.Height &&
         left.MinDepth == right.MinDepth && left.MaxDepth == right.MaxDepth;
}

class D3D11ViewportStateMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
    ASSERT_GE(context_.feature_level(), D3D_FEATURE_LEVEL_11_0);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ViewportStateMatrixSpec,
       RoundTrips4096CountCapacityAndViewportCombinations) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kViewportStateCaseCount);
  for (std::uint32_t logical = 0; logical < kViewportStateCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kViewportStateCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kViewportStateCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ViewportStateCase test = StateForCase(logical);
    std::array<D3D11_VIEWPORT, kMaxViewports> requested = {};
    for (UINT slot = 0; slot < test.bound_count; ++slot)
      requested[slot] = ViewportForCase(test.variant, slot);
    context_.context()->RSSetViewports(test.bound_count, requested.data());

    UINT count_only = 0;
    context_.context()->RSGetViewports(&count_only, nullptr);

    UINT zero_capacity = 0;
    D3D11_VIEWPORT zero_capacity_viewport = PoisonViewport();
    context_.context()->RSGetViewports(&zero_capacity, &zero_capacity_viewport);

    std::array<D3D11_VIEWPORT, kMaxViewports> returned;
    returned.fill(PoisonViewport());
    UINT returned_count = test.requested_capacity;
    context_.context()->RSGetViewports(&returned_count, returned.data());
    const UINT expected_returned_count =
        std::min(test.bound_count, test.requested_capacity);
    UINT failing_slot = kMaxViewports;
    for (UINT slot = 0; slot < kMaxViewports; ++slot) {
      const D3D11_VIEWPORT expected =
          slot >= test.requested_capacity
              ? PoisonViewport()
              : (slot < test.bound_count ? requested[slot] : ZeroViewport());
      if (!ViewportEquals(returned[slot], expected) &&
          failing_slot == kMaxViewports)
        failing_slot = slot;
    }

    context_.context()->RSSetViewports(0, nullptr);
    UINT cleared_count_only = 0;
    context_.context()->RSGetViewports(&cleared_count_only, nullptr);
    std::array<D3D11_VIEWPORT, kMaxViewports> cleared;
    cleared.fill(PoisonViewport());
    UINT cleared_count = kMaxViewports;
    context_.context()->RSGetViewports(&cleared_count, cleared.data());
    UINT uncleared_slot = kMaxViewports;
    for (UINT slot = 0; slot < kMaxViewports; ++slot) {
      if (!ViewportEquals(cleared[slot], ZeroViewport()) &&
          uncleared_slot == kMaxViewports)
        uncleared_slot = slot;
    }

    const bool count_matches = count_only == test.bound_count;
    const bool zero_capacity_matches =
        zero_capacity == 0 &&
        ViewportEquals(zero_capacity_viewport, PoisonViewport());
    const bool returned_count_matches =
        returned_count == expected_returned_count;
    const bool cleared_counts_match =
        cleared_count_only == 0 && cleared_count == 0;
    if (count_matches && zero_capacity_matches && returned_count_matches &&
        failing_slot == kMaxViewports && cleared_counts_match &&
        uncleared_slot == kMaxViewports)
      continue;

    const char *phase =
        !count_matches ? "count_only"
                       : (!zero_capacity_matches
                              ? "zero_capacity"
                              : (!returned_count_matches
                                     ? "returned_count"
                                     : (failing_slot != kMaxViewports
                                            ? "returned_viewport"
                                            : (!cleared_counts_match
                                                   ? "cleared_count"
                                                   : "cleared_viewport"))));
    const UINT diagnostic_slot =
        failing_slot != kMaxViewports
            ? failing_slot
            : (uncleared_slot != kMaxViewports ? uncleared_slot : 0u);
    D3D11_VIEWPORT expected_viewport = ZeroViewport();
    D3D11_VIEWPORT actual_viewport = ZeroViewport();
    if (!zero_capacity_matches) {
      expected_viewport = PoisonViewport();
      actual_viewport = zero_capacity_viewport;
    } else if (failing_slot != kMaxViewports) {
      expected_viewport =
          diagnostic_slot >= test.requested_capacity
              ? PoisonViewport()
              : (diagnostic_slot < test.bound_count ? requested[diagnostic_slot]
                                                    : ZeroViewport());
      actual_viewport = returned[diagnostic_slot];
    } else if (uncleared_slot != kMaxViewports) {
      actual_viewport = cleared[diagnostic_slot];
    }
    const auto case_id =
        dxmt::test::LogicalCaseId(kViewportStateCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kViewportStateCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 queue=Immediate "
                     "capability=RSSetViewports,RSGetViewports\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kViewportStateCases.family().traits.execution_path)
                  << '\n'
                  << "Parameters: logical=" << logical
                  << " bound_count=" << test.bound_count
                  << " requested_capacity=" << test.requested_capacity
                  << " variant=" << test.variant
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Observed: phase=" << phase << " slot=" << diagnostic_slot
                  << " count_only=" << count_only
                  << " zero_capacity=" << zero_capacity
                  << " returned_count=" << returned_count
                  << " expected_returned_count=" << expected_returned_count
                  << " cleared_count_only=" << cleared_count_only
                  << " cleared_count=" << cleared_count << '\n'
                  << "ExpectedViewport: (" << expected_viewport.TopLeftX << ','
                  << expected_viewport.TopLeftY << ','
                  << expected_viewport.Width << ',' << expected_viewport.Height
                  << ',' << expected_viewport.MinDepth << ','
                  << expected_viewport.MaxDepth << ") actual=("
                  << actual_viewport.TopLeftX << ',' << actual_viewport.TopLeftY
                  << ',' << actual_viewport.Width << ','
                  << actual_viewport.Height << ',' << actual_viewport.MinDepth
                  << ',' << actual_viewport.MaxDepth << ")\n"
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
  context_.context()->RSSetViewports(0, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
