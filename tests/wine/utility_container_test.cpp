#include <dxmt_test.hpp>

#include <adt.hpp>
#include <ftl.hpp>

#include "rc/util_rc_ptr.hpp"
#include "util_bloom.hpp"
#include "util_flags.hpp"
#include "util_hash.hpp"
#include "util_svector.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

enum class Feature : uint32_t { Read = 0, Write = 2, Atomic = 5 };

enum class Mask : uint32_t { None = 0, First = 1, Second = 2, Fourth = 4 };

TEST(Flags, SupportsSetQueriesAndBooleanAlgebra) {
  dxmt::Flags<Feature> flags(Feature::Read, Feature::Atomic);
  EXPECT_TRUE(flags.all(Feature::Read, Feature::Atomic));
  EXPECT_FALSE(flags.any(Feature::Write));

  flags.set(Feature::Write);
  EXPECT_EQ(flags.raw(), 0x25u);
  flags.clr(Feature::Read, Feature::Atomic);
  EXPECT_EQ(flags, dxmt::Flags<Feature>(Feature::Write));
  EXPECT_EQ((flags | dxmt::Flags<Feature>(Feature::Atomic)).raw(), 0x24u);
  EXPECT_EQ((flags ^ dxmt::Flags<Feature>(Feature::Write)).raw(), 0u);
  flags.clrAll();
  EXPECT_TRUE(flags.isClear());
}

TEST(EnumBitOperators, PreserveUnderlyingMasks) {
  constexpr auto combined = Mask::First | Mask::Fourth;
  static_assert(static_cast<uint32_t>(combined) == 5);
  static_assert(any_bit_set(combined & Mask::Fourth));
  static_assert(!any_bit_set(combined & Mask::Second));

  auto value = Mask::First;
  value |= Mask::Second;
  EXPECT_EQ(static_cast<uint32_t>(value), 3u);
  EXPECT_EQ(static_cast<uint32_t>(~Mask::None),
            std::numeric_limits<uint32_t>::max());
}

TEST(FunctionalUtilities, TransformsVectorsWithoutChangingInput) {
  const std::vector<int> input = {1, 2, 3};
  const auto output =
      input | [](int value) { return std::to_string(value * 2); };
  const auto stringify = [](int value) { return std::to_string(value); };
  const auto const_callable_output = input | stringify;

  EXPECT_EQ(input, (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(output, (std::vector<std::string>{"2", "4", "6"}));
  EXPECT_EQ(const_callable_output, (std::vector<std::string>{"1", "2", "3"}));
}

TEST(AlgebraicDataTypes, ConcatenatesTemplatesAndVisitsVariants) {
  using Combined =
      template_concat_t<std::variant<int, float>, std::variant<std::string>,
                        std::variant<uint64_t>>;
  static_assert(
      std::is_same_v<Combined,
                     std::variant<int, float, std::string, uint64_t>>);

  Combined value = std::string("dxmt");
  const auto length =
      std::visit(patterns{[](const std::string &text) { return text.size(); },
                          [](const auto &) { return size_t(0); }},
                 value);
  EXPECT_EQ(length, 4u);
}

TEST(HashState, CombinesValuesInOrder) {
  dxmt::HashState first;
  first.add(1);
  first.add(2);

  dxmt::HashState same;
  same.add(1);
  same.add(2);

  dxmt::HashState reversed;
  reversed.add(2);
  reversed.add(1);

  EXPECT_EQ(size_t(first), size_t(same));
  EXPECT_NE(size_t(first), size_t(reversed));
}

TEST(PartitionedBloomFilter, DetectsDisjointAndMergedKeys) {
  using Filter = dxmt::PartitionedBloomFilter64<16>;
  Filter::Key first_key;
  Filter::Key second_key;
  first_key.indices.fill(1);
  second_key.indices.fill(2);

  Filter first;
  Filter second;
  first.add(first_key);
  second.add(second_key);
  EXPECT_TRUE(first.isDisjointWith(second));

  second.add(first_key);
  EXPECT_FALSE(first.isDisjointWith(second));

  Filter merged;
  merged.merge(first);
  EXPECT_FALSE(first.isDisjointWith(merged));
}

TEST(PartitionedBloomFilter, GeneratesStableBoundedKeys) {
  const auto first = dxmt::PartitionedBloomFilter64<16>::generateNewKey(42);
  const auto same = dxmt::PartitionedBloomFilter64<16>::generateNewKey(42);
  const auto other = dxmt::PartitionedBloomFilter64<16>::generateNewKey(43);

  EXPECT_EQ(first.indices, same.indices);
  EXPECT_NE(first.indices, other.indices);
  EXPECT_TRUE(std::ranges::all_of(first.indices,
                                  [](uint8_t index) { return index < 64; }));
}

TEST(SmallVector, GrowsPastEmbeddedStorageAndPreservesOrder) {
  dxmt::small_vector<std::string, 2> values = {"a", "c"};
  EXPECT_TRUE(values.is_embedded());

  values.insert(values.begin() + 1, "b");
  values.emplace_back("d");
  EXPECT_FALSE(values.is_embedded());
  EXPECT_EQ(std::vector<std::string>(values.begin(), values.end()),
            (std::vector<std::string>{"a", "b", "c", "d"}));

  values.erase(1);
  values.pop_back();
  EXPECT_EQ(std::vector<std::string>(values.begin(), values.end()),
            (std::vector<std::string>{"a", "c"}));
  values.shrink_to_fit();
  EXPECT_TRUE(values.is_embedded());
  EXPECT_EQ(values.capacity(), 2u);
}

TEST(SmallVector, MovesNonCopyableAllocatedStorage) {
  dxmt::small_vector<std::unique_ptr<int>, 2> source;
  source.reserve(4);
  source.emplace_back(std::make_unique<int>(1));
  source.emplace_back(std::make_unique<int>(2));
  ASSERT_FALSE(source.is_embedded());

  dxmt::small_vector<std::unique_ptr<int>, 2> moved(std::move(source));
  EXPECT_TRUE(source.empty());
  ASSERT_EQ(moved.size(), 2u);
  EXPECT_EQ(*moved[0], 1);
  EXPECT_EQ(*moved[1], 2);
}

TEST(SmallVector, CopiesAllocatedStorageAndMovesEmbeddedElements) {
  dxmt::small_vector<std::string, 2> allocated = {"a", "b", "c"};
  ASSERT_FALSE(allocated.is_embedded());
  dxmt::small_vector<std::string, 4> copied(allocated);
  EXPECT_TRUE(copied.is_embedded());
  EXPECT_EQ(std::vector<std::string>(copied.begin(), copied.end()),
            (std::vector<std::string>{"a", "b", "c"}));

  dxmt::small_vector<std::unique_ptr<int>, 2> embedded;
  embedded.emplace_back(std::make_unique<int>(7));
  dxmt::small_vector<std::unique_ptr<int>, 4> moved(std::move(embedded));
  EXPECT_TRUE(embedded.empty());
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(*moved.front(), 7);
}

struct RefCountProbe {
  void incRef() { ++references; }
  void decRef() { --references; }
  int references = 0;
};

struct DerivedRefCountProbe : RefCountProbe {};

TEST(ReferenceCountedPointer, BalancesCopyMoveAndReset) {
  RefCountProbe object;
  {
    dxmt::Rc<RefCountProbe> first(&object);
    EXPECT_EQ(object.references, 1);
    {
      dxmt::Rc<RefCountProbe> copy(first);
      EXPECT_EQ(object.references, 2);
      dxmt::Rc<RefCountProbe> moved(std::move(copy));
      EXPECT_FALSE(copy);
      EXPECT_EQ(moved.ptr(), &object);
      EXPECT_EQ(object.references, 2);
    }
    EXPECT_EQ(object.references, 1);
    first = nullptr;
    EXPECT_EQ(object.references, 0);
  }
  EXPECT_EQ(object.references, 0);
}

TEST(ReferenceCountedPointer, SelfMovePreservesOwnership) {
  RefCountProbe object;
  dxmt::Rc<RefCountProbe> pointer(&object);
  auto &alias = pointer;
  pointer = std::move(alias);
  EXPECT_EQ(pointer.ptr(), &object);
  EXPECT_EQ(object.references, 1);
}

TEST(ReferenceCountedPointer, ConvertsDerivedCopiesAndMovesWithoutLeaks) {
  DerivedRefCountProbe object;
  {
    dxmt::Rc<DerivedRefCountProbe> derived(&object);
    dxmt::Rc<RefCountProbe> copied(derived);
    EXPECT_EQ(object.references, 2);

    dxmt::Rc<RefCountProbe> moved(std::move(derived));
    EXPECT_FALSE(derived);
    EXPECT_EQ(moved.ptr(), &object);
    EXPECT_EQ(object.references, 2);
  }
  EXPECT_EQ(object.references, 0);
}

} // namespace
