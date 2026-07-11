#include <dxmt_test.hpp>

#include <dxmt_descriptor_revision.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <semaphore>
#include <thread>
#include <vector>

namespace {

using dxmt::DescriptorContentRevision;
using dxmt::DescriptorRevisionClock;

struct CoordinatedRolloverHooks {
  std::binary_semaphore *rollover_claimed = nullptr;
  std::binary_semaphore *permit_rollover = nullptr;
  std::binary_semaphore *load_observed_rollover = nullptr;
  std::atomic<bool> *load_observation_published = nullptr;

  void OnRolloverClaimed() const noexcept {
    rollover_claimed->release();
    permit_rollover->acquire();
  }

  void OnLoadBlockedByRollover() const noexcept {
    if (!load_observation_published->exchange(true, std::memory_order_acq_rel))
      load_observed_rollover->release();
  }
};

TEST(DescriptorRevisionClock, RollsSequenceIntoANewEpoch) {
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
  DescriptorRevisionClock clock({7, max - 1});

  EXPECT_EQ(clock.Bump(), (DescriptorContentRevision{7, max}));
  EXPECT_EQ(clock.Bump(), (DescriptorContentRevision{8, 1}));
  EXPECT_EQ(clock.Load(), (DescriptorContentRevision{8, 1}));
}

TEST(DescriptorRevisionClock, ReportsEpochExhaustionWithoutWrapping) {
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
  DescriptorRevisionClock clock({max, max});
  DescriptorContentRevision revision = {17, 29};

  EXPECT_FALSE(clock.TryBump(revision));
  EXPECT_EQ(revision, (DescriptorContentRevision{17, 29}));
  EXPECT_EQ(clock.Load(), (DescriptorContentRevision{max, max}));
}

TEST(DescriptorRevisionClock, AssignsUniqueRevisionsAcrossWriters) {
  constexpr size_t thread_count = 8;
  constexpr size_t bumps_per_thread = 10000;
  DescriptorRevisionClock clock;
  std::vector<std::vector<DescriptorContentRevision>> revisions(thread_count);
  std::vector<std::thread> writers;
  writers.reserve(thread_count);

  for (size_t thread = 0; thread < thread_count; ++thread) {
    writers.emplace_back([&, thread] {
      auto &local = revisions[thread];
      local.reserve(bumps_per_thread);
      for (size_t i = 0; i < bumps_per_thread; ++i)
        local.push_back(clock.Bump());
    });
  }
  for (auto &writer : writers)
    writer.join();

  std::vector<DescriptorContentRevision> flattened;
  flattened.reserve(thread_count * bumps_per_thread);
  for (auto &local : revisions)
    flattened.insert(flattened.end(), local.begin(), local.end());
  std::sort(flattened.begin(), flattened.end(),
            [](const auto &a, const auto &b) {
              return a.epoch < b.epoch ||
                     (a.epoch == b.epoch && a.sequence < b.sequence);
            });

  ASSERT_EQ(flattened.size(), thread_count * bumps_per_thread);
  EXPECT_EQ(std::adjacent_find(flattened.begin(), flattened.end()),
            flattened.end());
}

TEST(DescriptorRevisionClock,
     AssignsUniqueOrderedRevisionsWhenWritersCrossAnEpoch) {
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t first_epoch = 23;
  constexpr size_t pre_rollover_bumps = 64;
  constexpr size_t thread_count = 8;
  constexpr size_t bumps_per_thread = 4096;
  constexpr size_t total_bumps = thread_count * bumps_per_thread;
  DescriptorRevisionClock clock({first_epoch, max - pre_rollover_bumps});
  std::vector<std::vector<DescriptorContentRevision>> revisions(thread_count);
  std::vector<std::thread> writers;
  writers.reserve(thread_count);

  for (size_t thread = 0; thread < thread_count; ++thread) {
    writers.emplace_back([&, thread] {
      auto &local = revisions[thread];
      local.reserve(bumps_per_thread);
      for (size_t i = 0; i < bumps_per_thread; ++i)
        local.push_back(clock.Bump());
    });
  }
  for (auto &writer : writers)
    writer.join();

  const auto less = [](const auto &a, const auto &b) {
    return a.epoch < b.epoch || (a.epoch == b.epoch && a.sequence < b.sequence);
  };
  for (const auto &local : revisions) {
    for (size_t i = 1; i < local.size(); ++i)
      EXPECT_TRUE(less(local[i - 1], local[i]));
  }

  std::vector<DescriptorContentRevision> flattened;
  flattened.reserve(total_bumps);
  for (auto &local : revisions)
    flattened.insert(flattened.end(), local.begin(), local.end());
  std::sort(flattened.begin(), flattened.end(), less);

  ASSERT_EQ(flattened.size(), total_bumps);
  EXPECT_EQ(std::adjacent_find(flattened.begin(), flattened.end()),
            flattened.end());

  size_t new_epoch_count = 0;
  for (const auto &revision : flattened) {
    if (revision.epoch == first_epoch) {
      EXPECT_GT(revision.sequence, max - pre_rollover_bumps);
      EXPECT_LE(revision.sequence, max);
    } else {
      EXPECT_EQ(revision.epoch, first_epoch + 1);
      ++new_epoch_count;
      EXPECT_EQ(revision.sequence, new_epoch_count);
    }
  }
  ASSERT_GT(new_epoch_count, 0u);
  EXPECT_EQ(clock.Load(),
            (DescriptorContentRevision{first_epoch + 1, new_epoch_count}));
}

TEST(DescriptorRevisionClock,
     LoadRejectsTheInProgressRolloverPairWithoutTearing) {
  constexpr uint64_t max = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t first_epoch = 41;
  std::binary_semaphore rollover_claimed{0};
  std::binary_semaphore permit_rollover{0};
  std::binary_semaphore load_observed_rollover{0};
  std::atomic<bool> load_observation_published{false};
  std::atomic<bool> load_returned{false};
  dxmt::BasicDescriptorRevisionClock<CoordinatedRolloverHooks> clock(
      {first_epoch, max},
      {&rollover_claimed, &permit_rollover, &load_observed_rollover,
       &load_observation_published});
  DescriptorContentRevision published = {};
  DescriptorContentRevision loaded = {};

  std::thread writer([&] { published = clock.Bump(); });

  rollover_claimed.acquire();
  std::thread reader([&] {
    loaded = clock.Load();
    load_returned.store(true, std::memory_order_release);
  });

  // The reader has executed Load while the transition counter is odd. It must
  // remain inside Load until the writer publishes a coherent epoch/sequence.
  load_observed_rollover.acquire();
  EXPECT_FALSE(load_returned.load(std::memory_order_acquire));
  permit_rollover.release();
  writer.join();
  reader.join();

  EXPECT_EQ(published, (DescriptorContentRevision{first_epoch + 1, 1}));
  EXPECT_EQ(loaded, published);
  EXPECT_EQ(clock.Load(), published);
}

TEST(DescriptorRevisionClock, OrdersPublicationByCommitRatherThanWriteStart) {
  DescriptorRevisionClock clock({17, 100});
  std::binary_semaphore heap_a_write_started{0};
  std::binary_semaphore permit_heap_a_commit{0};
  DescriptorContentRevision heap_a_commit = {};

  std::thread heap_a_writer([&] {
    // Heap A begins first but cannot publish until after heap B. Publication
    // order, rather than write-start order, must define cache invalidation.
    heap_a_write_started.release();
    permit_heap_a_commit.acquire();
    heap_a_commit = clock.Bump();
  });

  heap_a_write_started.acquire();
  const DescriptorContentRevision heap_b_commit = clock.Bump();
  const DescriptorContentRevision observed_after_heap_b = heap_b_commit;
  EXPECT_EQ(observed_after_heap_b, clock.Load());

  permit_heap_a_commit.release();
  heap_a_writer.join();
  EXPECT_EQ(heap_b_commit, (DescriptorContentRevision{17, 101}));
  EXPECT_EQ(heap_a_commit, (DescriptorContentRevision{17, 102}));
  // A consumer that observed B's completed publication is stale after A
  // commits. This models only the revision-clock ordering invariant; the D3D12
  // integration test separately exercises real descriptor heaps and replay.
  EXPECT_NE(observed_after_heap_b, clock.Load());
  EXPECT_EQ(clock.Load(), heap_a_commit);
}

} // namespace
