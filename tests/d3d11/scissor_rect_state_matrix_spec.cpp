#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 RSSetScissorRects / RSGetScissorRects state coverage. The
// logical matrix crosses every legal bound count, every legal getter capacity,
// and sixteen unique rectangle variants while also checking count-only,
// zero-capacity, unused-slot, and complete-unbind behavior.

namespace {

using dxmt::test::D3D11TestContext;

constexpr UINT kMaxScissorRects =
    D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
constexpr std::uint32_t kScissorRectStateCaseCount =
    kMaxScissorRects * kMaxScissorRects * kMaxScissorRects;
static_assert(kScissorRectStateCaseCount == 4096);

const dxmt::test::LogicalCaseFamilyRegistration kScissorRectStateCases(
    "D3D11ScissorRectStateMatrixSpec."
    "RoundTrips4096CountCapacityAndRectangleCombinations",
    "D3D11.RSGetScissorRects.State.", kScissorRectStateCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "Immediate", "RSSetScissorRects,RSGetScissorRects"},
     dxmt::test::kNormalTestCost,
     "one test-local immediate context and fixed arrays of at most sixteen "
     "D3D11 rectangles",
     "atomically bind every selected count and rectangle variant, query the "
     "count alone and with every output capacity, then bind zero rectangles",
     "the getter returns the exact count and rectangles, zero-fills requested "
     "unused slots, preserves slots beyond the requested capacity, and reports "
     "zero rectangles after unbinding",
     "logical ID, selected-case count, bound count, requested capacity, "
     "variant, returned counts, failing phase and slot, expected and actual "
     "rectangle, and exact replay argument"});

const dxmt::test::TestCostRegistration
    kScissorRectStateCost("D3D11ScissorRectStateMatrixSpec."
                          "RoundTrips4096CountCapacityAndRectangleCombinations",
                          dxmt::test::kNormalTestCost);

struct ScissorStateCase {
  UINT bound_count;
  UINT requested_capacity;
  UINT variant;
};

ScissorStateCase StateForCase(std::uint32_t logical) {
  return {1u + logical % kMaxScissorRects,
          1u + (logical / kMaxScissorRects) % kMaxScissorRects,
          logical / (kMaxScissorRects * kMaxScissorRects)};
}

D3D11_RECT RectForCase(std::uint32_t logical, UINT slot) {
  const LONG left = static_cast<LONG>((logical & 0xffu) * 8u + slot * 2u);
  const LONG top =
      static_cast<LONG>(((logical >> 8u) & 0xfu) * 64u + slot * 3u);
  const LONG width = 1 + static_cast<LONG>((logical >> 2u) & 3u);
  const LONG height = 1 + static_cast<LONG>((logical >> 4u) & 3u);
  return {left, top, left + width, top + height};
}

constexpr D3D11_RECT ZeroRect() { return {0, 0, 0, 0}; }

constexpr D3D11_RECT PoisonRect() { return {100001, 100002, 100003, 100004}; }

bool RectEquals(const D3D11_RECT &left, const D3D11_RECT &right) {
  return left.left == right.left && left.top == right.top &&
         left.right == right.right && left.bottom == right.bottom;
}

class D3D11ScissorRectStateMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11ScissorRectStateMatrixSpec,
       RoundTrips4096CountCapacityAndRectangleCombinations) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kScissorRectStateCaseCount);
  for (std::uint32_t logical = 0; logical < kScissorRectStateCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kScissorRectStateCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kScissorRectStateCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ScissorStateCase test = StateForCase(logical);
    std::array<D3D11_RECT, kMaxScissorRects> requested = {};
    for (UINT slot = 0; slot < test.bound_count; ++slot)
      requested[slot] = RectForCase(logical, slot);
    context_.context()->RSSetScissorRects(test.bound_count, requested.data());

    UINT count_only = ~0u;
    context_.context()->RSGetScissorRects(&count_only, nullptr);

    UINT zero_capacity = 0;
    D3D11_RECT zero_capacity_rect = PoisonRect();
    context_.context()->RSGetScissorRects(&zero_capacity, &zero_capacity_rect);

    std::array<D3D11_RECT, kMaxScissorRects> returned;
    returned.fill(PoisonRect());
    UINT returned_count = test.requested_capacity;
    context_.context()->RSGetScissorRects(&returned_count, returned.data());
    const UINT expected_returned_count =
        std::min(test.bound_count, test.requested_capacity);
    UINT failing_slot = kMaxScissorRects;
    for (UINT slot = 0; slot < kMaxScissorRects; ++slot) {
      const D3D11_RECT expected =
          slot >= test.requested_capacity
              ? PoisonRect()
              : (slot < test.bound_count ? requested[slot] : ZeroRect());
      if (!RectEquals(returned[slot], expected) &&
          failing_slot == kMaxScissorRects)
        failing_slot = slot;
    }

    context_.context()->RSSetScissorRects(0, nullptr);
    UINT cleared_count_only = ~0u;
    context_.context()->RSGetScissorRects(&cleared_count_only, nullptr);
    std::array<D3D11_RECT, kMaxScissorRects> cleared;
    cleared.fill(PoisonRect());
    UINT cleared_count = kMaxScissorRects;
    context_.context()->RSGetScissorRects(&cleared_count, cleared.data());
    UINT uncleared_slot = kMaxScissorRects;
    for (UINT slot = 0; slot < kMaxScissorRects; ++slot) {
      if (!RectEquals(cleared[slot], ZeroRect()) &&
          uncleared_slot == kMaxScissorRects)
        uncleared_slot = slot;
    }

    const bool count_matches = count_only == test.bound_count;
    const bool zero_capacity_matches =
        zero_capacity == 0 && RectEquals(zero_capacity_rect, PoisonRect());
    const bool returned_count_matches =
        returned_count == expected_returned_count;
    const bool cleared_counts_match =
        cleared_count_only == 0 && cleared_count == 0;
    if (count_matches && zero_capacity_matches && returned_count_matches &&
        failing_slot == kMaxScissorRects && cleared_counts_match &&
        uncleared_slot == kMaxScissorRects)
      continue;

    const char *phase =
        !count_matches
            ? "count_only"
            : (!zero_capacity_matches
                   ? "zero_capacity"
                   : (!returned_count_matches
                          ? "returned_count"
                          : (failing_slot != kMaxScissorRects
                                 ? "returned_rect"
                                 : (!cleared_counts_match ? "cleared_count"
                                                          : "cleared_rect"))));
    const UINT diagnostic_slot =
        failing_slot != kMaxScissorRects
            ? failing_slot
            : (uncleared_slot != kMaxScissorRects ? uncleared_slot : 0u);
    D3D11_RECT expected_rect = ZeroRect();
    D3D11_RECT actual_rect = ZeroRect();
    if (!zero_capacity_matches) {
      expected_rect = PoisonRect();
      actual_rect = zero_capacity_rect;
    } else if (failing_slot != kMaxScissorRects) {
      expected_rect =
          diagnostic_slot >= test.requested_capacity
              ? PoisonRect()
              : (diagnostic_slot < test.bound_count ? requested[diagnostic_slot]
                                                    : ZeroRect());
      actual_rect = returned[diagnostic_slot];
    } else if (uncleared_slot != kMaxScissorRects) {
      actual_rect = cleared[diagnostic_slot];
    }
    const auto case_id =
        dxmt::test::LogicalCaseId(kScissorRectStateCases.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Class: "
                  << dxmt::test::TestClassName(
                         kScissorRectStateCases.family().traits.test_class)
                  << '\n'
                  << "Requirements: feature_level=11_0 queue=Immediate "
                     "capability=RSSetScissorRects,RSGetScissorRects\n"
                  << "ExecutionPath: "
                  << dxmt::test::ExecutionPathName(
                         kScissorRectStateCases.family().traits.execution_path)
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
                  << "ExpectedRect: (" << expected_rect.left << ','
                  << expected_rect.top << ',' << expected_rect.right << ','
                  << expected_rect.bottom << ") actual=(" << actual_rect.left
                  << ',' << actual_rect.top << ',' << actual_rect.right << ','
                  << actual_rect.bottom << ")\n"
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }
  context_.context()->RSSetScissorRects(0, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
