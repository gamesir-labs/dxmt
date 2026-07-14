#include <dxmt_test.hpp>

#include "dxmt_binding_set.hpp"

#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

namespace dxmt {

struct TestBinding {
  int value = 0;
};

template <> struct redundant_binding_trait<TestBinding> {
  static bool is_redundant(const TestBinding &left, const TestBinding &right) {
    return left.value == right.value;
  }
};

} // namespace dxmt

namespace {

TEST(BindingSet, TracksReplacementBoundAndDirtyState) {
  dxmt::BindingSet<dxmt::TestBinding, 8> bindings;
  EXPECT_FALSE(bindings.any_bound());
  EXPECT_FALSE(bindings.any_dirty());

  bool replaced = false;
  auto &stored = bindings.bind(3, dxmt::TestBinding{7}, replaced);
  EXPECT_TRUE(replaced);
  EXPECT_EQ(stored.value, 7);
  EXPECT_TRUE(bindings.test_bound(3));
  EXPECT_TRUE(bindings.test_dirty(3));

  bindings.clear_dirty();
  replaced = false;
  auto &same = bindings.bind(3, dxmt::TestBinding{7}, replaced);
  EXPECT_FALSE(replaced);
  EXPECT_EQ(&same, &stored);
  EXPECT_FALSE(bindings.test_dirty(3));

  bindings.bind(3, dxmt::TestBinding{9}, replaced);
  EXPECT_TRUE(replaced);
  EXPECT_EQ(bindings[3].value, 9);
  EXPECT_TRUE(bindings.test_dirty(3));
}

TEST(BindingSet, UnbindIsIdempotentAndClearsStorage) {
  dxmt::BindingSet<dxmt::TestBinding, 8> bindings;
  bool replaced = false;
  bindings.bind(2, dxmt::TestBinding{11}, replaced);
  bindings.clear_dirty();

  EXPECT_TRUE(bindings.unbind(2));
  EXPECT_FALSE(bindings.test_bound(2));
  EXPECT_TRUE(bindings.test_dirty(2));
  EXPECT_EQ(bindings.at(2).value, 0);
  EXPECT_FALSE(bindings.unbind(2));
}

TEST(BindingSet, IteratesBoundAndHazardSlotsAcrossQwords) {
  dxmt::BindingSet<dxmt::TestBinding, 130> bindings;
  for (const auto [slot, value, hazard] :
       {std::tuple{1u, 10, true}, std::tuple{64u, 20, false},
        std::tuple{129u, 30, true}}) {
    bool replaced = false;
    bindings.bind(slot, dxmt::TestBinding{value}, replaced, hazard);
  }

  std::vector<std::pair<size_t, int>> bound;
  for (const auto [slot, value] : bindings)
    bound.emplace_back(slot, value.value);
  EXPECT_EQ(bound, (std::vector<std::pair<size_t, int>>{
                       {1, 10}, {64, 20}, {129, 30}}));

  std::vector<size_t> hazards;
  for (auto it = bindings.hazard_begin(); it != bindings.hazard_end(); ++it)
    hazards.push_back((*it).first);
  EXPECT_EQ(hazards, (std::vector<size_t>{1, 129}));
}

TEST(BindingSet, AppliesDirtyAndBoundMasks) {
  dxmt::BindingSet<dxmt::TestBinding, 128> bindings;
  bool replaced = false;
  bindings.bind(1, dxmt::TestBinding{1}, replaced);
  bindings.bind(65, dxmt::TestBinding{2}, replaced);

  EXPECT_TRUE(bindings.any_dirty_masked(uint64_t(1) << 1));
  EXPECT_FALSE(bindings.any_dirty_masked(uint64_t(1) << 2));
  EXPECT_TRUE(bindings.any_dirty_masked(uint64_t(1) << 1, uint64_t(1) << 1));
  EXPECT_TRUE(bindings.all_bound_masked(uint32_t(1) << 1));
  EXPECT_FALSE(
      bindings.all_bound_masked((uint32_t(1) << 1) | (uint32_t(1) << 2)));

  bindings.clear_dirty_mask(uint32_t(1) << 1);
  EXPECT_FALSE(bindings.test_dirty(1));
  EXPECT_TRUE(bindings.test_dirty(65));
}

TEST(BindingSet, MoveMarksEveryBoundSlotDirty) {
  dxmt::BindingSet<dxmt::TestBinding, 8> source;
  bool replaced = false;
  source.bind(4, dxmt::TestBinding{42}, replaced);
  source.clear_dirty();

  dxmt::BindingSet<dxmt::TestBinding, 8> moved(std::move(source));
  EXPECT_TRUE(moved.test_bound(4));
  EXPECT_TRUE(moved.test_dirty(4));
  EXPECT_EQ(moved[4].value, 42);
}

TEST(BindingSet, MoveAssignmentReplacesAllStateAndMarksBindingsDirty) {
  dxmt::BindingSet<dxmt::TestBinding, 130> source;
  bool replaced = false;
  source.bind(0, dxmt::TestBinding{10}, replaced, false);
  source.bind(65, dxmt::TestBinding{20}, replaced, true);
  source.bind(129, dxmt::TestBinding{30}, replaced, false);
  source.clear_dirty();

  dxmt::BindingSet<dxmt::TestBinding, 130> destination;
  destination.bind(1, dxmt::TestBinding{99}, replaced, true);
  destination = std::move(source);

  EXPECT_FALSE(destination.test_bound(1));
  for (const auto slot : {0u, 65u, 129u}) {
    EXPECT_TRUE(destination.test_bound(slot));
    EXPECT_TRUE(destination.test_dirty(slot));
  }

  std::vector<size_t> hazards;
  for (auto it = destination.hazard_begin(); it != destination.hazard_end();
       ++it)
    hazards.push_back((*it).first);
  EXPECT_EQ(hazards, (std::vector<size_t>{65}));

  std::vector<int> values;
  for (const auto [slot, value] : destination)
    values.push_back(value.value);
  EXPECT_EQ(values, (std::vector<int>{10, 20, 30}));
}

} // namespace
