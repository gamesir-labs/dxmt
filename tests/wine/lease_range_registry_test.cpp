#include <dxmt_test.hpp>

#include <dxmt_lease_range_registry.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>

namespace {

struct LeaseRecord {
  static constexpr uint32_t kMagic = 0x4c454153;
  uint32_t magic = kMagic;
};

struct LeaseOwner {
  // One external reference and one registry reference.
  std::atomic<uint32_t> references{2};
  std::atomic<bool> destroyed{false};

  void Retain() { references.fetch_add(1, std::memory_order_acq_rel); }

  void Release() {
    if (references.fetch_sub(1, std::memory_order_acq_rel) == 1)
      destroyed.store(true, std::memory_order_release);
  }
};

class TestLease {
public:
  TestLease() = default;
  TestLease(const TestLease &) = delete;
  TestLease &operator=(const TestLease &) = delete;

  TestLease(TestLease &&other) noexcept
      : record_(other.record_), owner_(other.owner_) {
    other.record_ = nullptr;
    other.owner_ = nullptr;
  }

  TestLease &operator=(TestLease &&other) noexcept {
    if (this == &other)
      return *this;
    Reset();
    record_ = other.record_;
    owner_ = other.owner_;
    other.record_ = nullptr;
    other.owner_ = nullptr;
    return *this;
  }

  ~TestLease() { Reset(); }

  static TestLease Acquire(LeaseRecord *record, LeaseOwner *owner) {
    owner->Retain();
    return TestLease(record, owner);
  }

  explicit operator bool() const { return record_ != nullptr; }
  LeaseRecord *get() const { return record_; }

  void Reset() {
    auto *owner = owner_;
    record_ = nullptr;
    owner_ = nullptr;
    if (owner)
      owner->Release();
  }

private:
  TestLease(LeaseRecord *record, LeaseOwner *owner)
      : record_(record), owner_(owner) {}

  LeaseRecord *record_ = nullptr;
  LeaseOwner *owner_ = nullptr;
};

using Registry = dxmt::LeaseRangeRegistry<LeaseRecord, LeaseOwner>;

auto ValidRecord = [](const LeaseRecord *record) {
  return record->magic == LeaseRecord::kMagic;
};

TEST(LeaseRangeRegistry, RejectsForeignMisalignedAndInvalidRecords) {
  Registry registry;
  LeaseOwner owner;
  LeaseOwner foreign_owner;
  std::array<LeaseRecord, 3> records = {};

  ASSERT_TRUE(registry.Register(records.data(), records.size(), &owner));
  EXPECT_FALSE(registry.Register(records.data(), records.size(), &owner));
  EXPECT_FALSE(registry.Unregister(records.data(), &foreign_owner));

  auto acquire = [](LeaseRecord *record, LeaseOwner *lease_owner) {
    return TestLease::Acquire(record, lease_owner);
  };
  const uintptr_t begin = reinterpret_cast<uintptr_t>(records.data());
  EXPECT_FALSE(registry.Lookup(0, ValidRecord, acquire));
  EXPECT_FALSE(registry.Lookup(begin + 1, ValidRecord, acquire));
  EXPECT_FALSE(registry.Lookup(begin + sizeof(records), ValidRecord, acquire));

  records[1].magic = 0;
  EXPECT_FALSE(
      registry.Lookup(begin + sizeof(LeaseRecord), ValidRecord, acquire));
  records[1].magic = LeaseRecord::kMagic;
  auto lease =
      registry.Lookup(begin + sizeof(LeaseRecord), ValidRecord, acquire);
  ASSERT_TRUE(lease);
  EXPECT_EQ(lease.get(), &records[1]);
  lease.Reset();

  EXPECT_TRUE(registry.Unregister(records.data(), &owner));
  owner.Release();
  owner.Release();
  EXPECT_TRUE(owner.destroyed.load(std::memory_order_acquire));

  foreign_owner.Release();
  foreign_owner.Release();
}

TEST(LeaseRangeRegistry, RejectsNullEmptyAndOverflowingRegistrations) {
  Registry registry;
  LeaseOwner owner;
  LeaseRecord record;

  EXPECT_FALSE(registry.Register(nullptr, 1, &owner));
  EXPECT_FALSE(registry.Register(&record, 0, &owner));
  EXPECT_FALSE(registry.Register(&record, 1, nullptr));
  EXPECT_FALSE(registry.Unregister(nullptr, &owner));
  EXPECT_FALSE(registry.Unregister(&record, nullptr));
  EXPECT_FALSE(registry.TryUnregister(nullptr, &owner));
  EXPECT_FALSE(registry.TryUnregister(&record, nullptr));

  const auto overflowing_count =
      std::size_t{UINTPTR_MAX / sizeof(LeaseRecord)} + 1u;
  EXPECT_FALSE(registry.Register(reinterpret_cast<LeaseRecord *>(1),
                                 overflowing_count, &owner));
  auto *wrapping_range = reinterpret_cast<LeaseRecord *>(
      UINTPTR_MAX - sizeof(LeaseRecord) + 1u);
  EXPECT_FALSE(registry.Register(wrapping_range, 1, &owner));
}

TEST(LeaseRangeRegistry,
     LeaseAcquisitionExcludesUnregistrationUntilOwnerIsRetained) {
  Registry registry;
  LeaseOwner owner;
  std::array<LeaseRecord, 2> records = {};
  ASSERT_TRUE(registry.Register(records.data(), records.size(), &owner));

  std::binary_semaphore acquire_entered{0};
  std::binary_semaphore permit_acquire{0};
  std::atomic<bool> owner_was_alive{false};
  TestLease lease;

  const uintptr_t address = reinterpret_cast<uintptr_t>(&records[1]);
  std::thread lookup_thread([&] {
    lease =
        registry.Lookup(address, ValidRecord,
                        [&](LeaseRecord *record, LeaseOwner *lease_owner) {
                          // This pause is entirely in the unit-test callback.
                          // Lookup must keep its shared registry lock while the
                          // callback acquires ownership.
                          acquire_entered.release();
                          permit_acquire.acquire();
                          owner_was_alive.store(!lease_owner->destroyed.load(
                                                    std::memory_order_acquire),
                                                std::memory_order_release);
                          return TestLease::Acquire(record, lease_owner);
                        });
  });

  acquire_entered.acquire();
  // The callback is paused after Lookup took its shared lock but before the
  // owner retain. A non-blocking exclusive erase must deterministically fail.
  EXPECT_FALSE(registry.TryUnregister(records.data(), &owner));
  permit_acquire.release();
  lookup_thread.join();

  ASSERT_TRUE(lease);
  EXPECT_TRUE(owner_was_alive.load(std::memory_order_acquire));

  EXPECT_TRUE(registry.Unregister(records.data(), &owner));
  // Model final public Release dropping registry and public ownership.
  owner.Release();
  owner.Release();
  EXPECT_EQ(owner.references.load(std::memory_order_acquire), 1u);
  EXPECT_FALSE(owner.destroyed.load(std::memory_order_acquire));

  // Unregistration is immediately visible even though the acquired lease
  // still keeps the owner and record storage alive.
  auto stale = registry.Lookup(
      address, ValidRecord, [](LeaseRecord *record, LeaseOwner *lease_owner) {
        return TestLease::Acquire(record, lease_owner);
      });
  EXPECT_FALSE(stale);

  lease.Reset();
  EXPECT_EQ(owner.references.load(std::memory_order_acquire), 0u);
  EXPECT_TRUE(owner.destroyed.load(std::memory_order_acquire));
}

TEST(LeaseRangeRegistry, AcceptsAdjacentRangesAndRejectsEveryOverlap) {
  Registry registry;
  LeaseOwner first_owner;
  LeaseOwner second_owner;
  std::array<LeaseRecord, 6> records = {};
  ASSERT_TRUE(registry.Register(records.data(), 3, &first_owner));
  ASSERT_TRUE(registry.Register(records.data() + 3, 3, &second_owner));

  EXPECT_FALSE(registry.Register(records.data() + 1, 2, &second_owner));
  EXPECT_FALSE(registry.Register(records.data() + 2, 2, &second_owner));
  EXPECT_FALSE(registry.Register(records.data() + 4, 2, &first_owner));

  auto acquire = [](LeaseRecord *record, LeaseOwner *owner) {
    return TestLease::Acquire(record, owner);
  };
  auto first = registry.Lookup(reinterpret_cast<uintptr_t>(&records[2]),
                               ValidRecord, acquire);
  auto second = registry.Lookup(reinterpret_cast<uintptr_t>(&records[3]),
                                ValidRecord, acquire);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.get(), &records[2]);
  EXPECT_EQ(second.get(), &records[3]);
  first.Reset();
  second.Reset();

  EXPECT_TRUE(registry.Unregister(records.data(), &first_owner));
  EXPECT_TRUE(registry.Unregister(records.data() + 3, &second_owner));
  first_owner.Release();
  first_owner.Release();
  second_owner.Release();
  second_owner.Release();
}

TEST(LeaseRangeRegistry, ReusesAnUnregisteredAddressWithANewOwner) {
  Registry registry;
  LeaseOwner first_owner;
  LeaseOwner second_owner;
  std::array<LeaseRecord, 2> records = {};
  ASSERT_TRUE(registry.Register(records.data(), records.size(), &first_owner));
  ASSERT_TRUE(registry.Unregister(records.data(), &first_owner));
  ASSERT_TRUE(
      registry.Register(records.data(), records.size(), &second_owner));
  EXPECT_FALSE(registry.Unregister(records.data(), &first_owner));

  auto lease = registry.Lookup(
      reinterpret_cast<uintptr_t>(&records[1]), ValidRecord,
      [](LeaseRecord *record, LeaseOwner *owner) {
        return TestLease::Acquire(record, owner);
      });
  ASSERT_TRUE(lease);
  EXPECT_EQ(lease.get(), &records[1]);
  lease.Reset();

  EXPECT_TRUE(registry.Unregister(records.data(), &second_owner));
  first_owner.Release();
  first_owner.Release();
  second_owner.Release();
  second_owner.Release();
}

} // namespace
