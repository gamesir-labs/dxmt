#include <dxmt_test.hpp>

#include "thread.hpp"
#include "threadpool.hpp"
#include "util_cpu_fence.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct ThreadpoolWork {
  std::atomic<uint32_t> *completed;
};

struct ThreadpoolTrait {
  using work_type = ThreadpoolWork;

  void invoke_work(work_type *work) { ++*work->completed; }
};

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

TEST(Threadpool, ExecutesQueuedWorkAndMakesWaitIdempotent) {
  dxmt::threadpool<ThreadpoolTrait> pool;
  std::atomic<uint32_t> completed = 0;
  std::array<ThreadpoolWork, 12> work;
  std::array<dxmt::threadpool<ThreadpoolTrait>::work_handle, work.size()>
      handles;

  for (size_t i = 0; i < work.size(); ++i) {
    work[i].completed = &completed;
    ASSERT_EQ(pool.enqueue(&work[i], &handles[i]), S_OK);
  }
  for (auto &handle : handles) {
    pool.wait(&handle);
    EXPECT_TRUE(handle.done);
    EXPECT_EQ(handle.work, nullptr);
    pool.wait(&handle);
  }
  EXPECT_EQ(completed.load(), work.size());
}

TEST(Threadpool, RejectsMissingWorkOrOutputHandle) {
  dxmt::threadpool<ThreadpoolTrait> pool;
  ThreadpoolWork work{};
  dxmt::threadpool<ThreadpoolTrait>::work_handle handle;

  EXPECT_EQ(pool.enqueue(nullptr, &handle), E_POINTER);
  EXPECT_EQ(pool.enqueue(&work, nullptr), E_POINTER);
}

} // namespace
