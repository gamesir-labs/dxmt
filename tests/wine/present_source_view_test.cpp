#include <dxmt_test.hpp>

#include "dxmt_present_source_view.hpp"

#include <atomic>
#include <thread>

namespace {

TEST(PresentSourceViewState, ResolvesTheValuePublishedBeforeQueuedWorkRuns) {
  dxmt::PresentSourceViewState state;
  constexpr uint64_t kValueAtPresentCall = 0x1111;
  constexpr uint64_t kValuePublishedByEarlierReplay = 0x2222;

  state.publish(kValueAtPresentCall);
  const auto eager_snapshot = state.resolve();
  const auto queued_resolve = [&state] { return state.resolve(); };

  state.publish(kValuePublishedByEarlierReplay);

  EXPECT_EQ(eager_snapshot, kValueAtPresentCall);
  EXPECT_EQ(queued_resolve(), kValuePublishedByEarlierReplay);
}

TEST(PresentSourceViewState, PublishesAcrossReplayAndPresentThreads) {
  dxmt::PresentSourceViewState state;
  std::atomic<bool> replay_completed = false;
  constexpr uint64_t kTypedRenderTargetView = 0x123456789abcdef0ull;

  std::thread replay([&] {
    state.publish(kTypedRenderTargetView);
    replay_completed.store(true, std::memory_order_release);
  });
  while (!replay_completed.load(std::memory_order_acquire))
    std::this_thread::yield();

  EXPECT_EQ(state.resolve(), kTypedRenderTargetView);
  replay.join();
}

} // namespace
