#include <dxmt_test.hpp>

#include "dxmt_tasks.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {

class ScheduledTask {
public:
  explicit ScheduledTask(ScheduledTask *dependency = nullptr)
      : dependency_(dependency) {}

  ScheduledTask *run() {
    {
      std::lock_guard lock(mutex_);
      ++runs_;
    }
    condition_.notify_all();
    if (dependency_ && !dependency_->done())
      return dependency_;
    return this;
  }

  bool done() const {
    std::lock_guard lock(mutex_);
    return done_;
  }

  void complete() {
    {
      std::lock_guard lock(mutex_);
      done_ = true;
    }
    condition_.notify_all();
  }

  bool waitForRuns(uint32_t count) {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(2),
                               [this, count] { return runs_ >= count; });
  }

  bool waitUntilDone() {
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, std::chrono::seconds(2),
                               [this] { return done_; });
  }

  uint32_t runs() const {
    std::lock_guard lock(mutex_);
    return runs_;
  }

private:
  ScheduledTask *dependency_;
  uint32_t runs_ = 0;
  bool done_ = false;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
};

} // namespace

namespace dxmt {

template <> struct task_trait<ScheduledTask *> {
  ScheduledTask *run_task(ScheduledTask *task) { return task->run(); }
  bool get_done(ScheduledTask *task) { return task->done(); }
  void set_done(ScheduledTask *task) { task->complete(); }
};

} // namespace dxmt

TEST(TaskScheduler, RunsIndependentTasksToCompletion) {
  std::array<ScheduledTask, 8> tasks;
  dxmt::task_scheduler<ScheduledTask *> scheduler;
  for (auto &task : tasks)
    scheduler.submit(&task);
  for (auto &task : tasks) {
    ASSERT_TRUE(task.waitUntilDone());
    EXPECT_EQ(task.runs(), 1u);
  }
}

TEST(TaskScheduler, ResumesTaskAfterDependencyCompletes) {
  ScheduledTask prerequisite;
  ScheduledTask dependent(&prerequisite);
  dxmt::task_scheduler<ScheduledTask *> scheduler;

  scheduler.submit(&dependent);
  ASSERT_TRUE(dependent.waitForRuns(1));
  EXPECT_FALSE(dependent.done());

  scheduler.submit(&prerequisite);
  ASSERT_TRUE(prerequisite.waitUntilDone());
  ASSERT_TRUE(dependent.waitUntilDone());
  EXPECT_GE(dependent.runs(), 2u);
}
