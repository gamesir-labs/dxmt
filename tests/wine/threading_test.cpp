#include <dxmt_test.hpp>

#include "thread.hpp"
#include "util_cpu_fence.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

TEST(CpuFence, SignalsMonotonicallyAndWakesAllSatisfiedWaiters) {
  dxmt::CpuFence fence(3);
  fence.signal(2);
  EXPECT_EQ(fence.signaledValue(), 3u);

  std::atomic<uint32_t> completed = 0;
  dxmt::thread first([&] {
    fence.wait(5);
    ++completed;
  });
  dxmt::thread second([&] {
    fence.wait(4);
    ++completed;
  });

  fence.signal(5);
  first.join();
  second.join();
  EXPECT_EQ(completed.load(), 2u);
  EXPECT_EQ(fence.signaledValue(), 5u);
}

TEST(ConditionVariable, WaitUntilUsesTheFutureDeadline) {
  dxmt::mutex mutex;
  dxmt::condition_variable condition;
  dxmt::thread delayed_notification([&] {
    std::this_thread::sleep_for(200ms);
    condition.notify_all();
  });

  std::unique_lock lock(mutex);
  const auto status =
      condition.wait_until(lock, std::chrono::steady_clock::now() + 30ms);
  lock.unlock();
  delayed_notification.join();

  EXPECT_EQ(status, std::cv_status::timeout);
}

TEST(ThreadWrapper, MoveTransfersJoinableOwnership) {
  std::atomic<bool> ran = false;
  dxmt::thread source([&] { ran = true; });
  dxmt::thread destination(std::move(source));

  EXPECT_FALSE(source.joinable());
  EXPECT_TRUE(destination.joinable());
  destination.join();
  EXPECT_TRUE(ran.load());
}

} // namespace
