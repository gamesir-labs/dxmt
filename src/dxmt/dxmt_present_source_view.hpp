#pragma once

#include <atomic>
#include <cstdint>

namespace dxmt {

class PresentSourceViewState {
public:
  void publish(uint64_t view) noexcept {
    view_.store(view, std::memory_order_release);
  }

  uint64_t resolve() const noexcept {
    return view_.load(std::memory_order_acquire);
  }

private:
  std::atomic<uint64_t> view_{0};
};

} // namespace dxmt
