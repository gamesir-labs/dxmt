#include "d3d11_multithread.hpp"
#include "thread.hpp"
#include "util_bit.hpp"
#include "util_likely.hpp"
#include <atomic>

namespace dxmt {

namespace {

class transition_guard {
public:
  explicit transition_guard(std::atomic_flag &transition)
      : transition_(transition) {
    while (transition_.test_and_set(std::memory_order_acquire))
      dxmt::this_thread::yield();
  }

  ~transition_guard() {
    transition_.clear(std::memory_order_release);
  }

private:
  std::atomic_flag &transition_;
};

} // namespace

void d3d11_device_mutex::lock() {
  const uint32_t tid = dxmt::this_thread::get_id();

  // Recursive entry must not wait for the protection-transition gate. A
  // contending thread may briefly hold that gate while observing this thread
  // as the owner; taking the gate first here would create an ABBA deadlock.
  if (owner_.load(std::memory_order_acquire) == tid) {
    counter_++;
    return;
  }

  for (;;) {
    {
      transition_guard transition(transition_);
      if (!protected_.load(std::memory_order_acquire))
        return;

      uint32_t expected = 0;
      if (owner_.compare_exchange_strong(expected, tid,
                                         std::memory_order_acquire)) {
        counter_ = 1;
        return;
      }
      if (unlikely(expected == tid)) {
        counter_++;
        return;
      }
    }

    // Never retain transition_ while waiting for the current owner: the owner
    // is allowed to enter the recursive lock before it releases the outermost
    // critical section.
    while (owner_.load(std::memory_order_relaxed)) {
#if defined(DXMT_ARCH_X86)
      _mm_pause();
#elif defined(DXMT_ARCH_ARM64)
      __asm__ __volatile__("yield");
#else
// do nothing
#endif
    }
  }
}
void d3d11_device_mutex::unlock() noexcept {
  const uint32_t tid = dxmt::this_thread::get_id();
  if (owner_.load(std::memory_order_acquire) != tid)
    return;
  counter_--;
  if (likely(counter_ == 0))
    owner_.store(0, std::memory_order_release);
}

bool d3d11_device_mutex::try_lock() {
  const uint32_t tid = dxmt::this_thread::get_id();
  if (owner_.load(std::memory_order_acquire) == tid) {
    counter_++;
    return true;
  }

  transition_guard transition(transition_);
  if (!protected_.load(std::memory_order_acquire))
    return true;

  uint32_t expected = 0;
  if (owner_.compare_exchange_strong(expected, tid,
                                     std::memory_order_acquire)) {
    counter_ = 1;
    return true;
  }
  if (expected != tid)
    return false;
  counter_++;
  return true;
}

bool d3d11_device_mutex::set_protected(bool Protected) {
  transition_guard transition(transition_);
  const bool previous = protected_.load(std::memory_order_acquire);
  if (previous == Protected)
    return previous;

  if (!Protected) {
    const uint32_t tid = dxmt::this_thread::get_id();
    if (owner_.load(std::memory_order_acquire) == tid) {
      // Leave owner_/counter_ intact so the outstanding recursive Enter calls
      // can still unwind after protection is disabled.
      protected_.store(false, std::memory_order_release);
      return previous;
    }
    while (owner_.load(std::memory_order_acquire))
      dxmt::this_thread::yield();
  }
  protected_.store(Protected, std::memory_order_release);
  return previous;
}
bool d3d11_device_mutex::get_protected() {
  return protected_.load(std::memory_order_acquire);
}

}; // namespace dxmt
