#include <dxmt_test.hpp>

#include "dxmt_capture.hpp"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace {

class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(const char *name, const char *value) : name_(name) {
    const DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
    if (length) {
      std::vector<char> buffer(length);
      GetEnvironmentVariableA(name, buffer.data(), buffer.size());
      previous_ = buffer.data();
    }
    SetEnvironmentVariableA(name, value);
  }

  ~ScopedEnvironmentVariable() {
    SetEnvironmentVariableA(name_.c_str(),
                            previous_ ? previous_->c_str() : nullptr);
  }

private:
  std::string name_;
  std::optional<std::string> previous_;
};

TEST(CaptureState, DisabledEnvironmentKeepsCaptureFrozen) {
  ScopedEnvironmentVariable capture_enabled("MTL_CAPTURE_ENABLED", nullptr);
  dxmt::CaptureState state;

  state.scheduleNextFrameCapture(4);
  EXPECT_EQ(state.getNextAction(4), dxmt::CaptureState::NextAction::Nothing);
}

TEST(CaptureState, CapturesExactlyOneFrameAndReturnsToIdle) {
  ScopedEnvironmentVariable capture_enabled("MTL_CAPTURE_ENABLED", "1");
  dxmt::CaptureState state;

  state.scheduleNextFrameCapture(7);
  EXPECT_EQ(state.getNextAction(6), dxmt::CaptureState::NextAction::Nothing);
  EXPECT_EQ(state.getNextAction(7),
            dxmt::CaptureState::NextAction::StartCapture);
  state.scheduleNextFrameCapture(8);
  EXPECT_EQ(state.getNextAction(7), dxmt::CaptureState::NextAction::Nothing);
  EXPECT_EQ(state.getNextAction(8),
            dxmt::CaptureState::NextAction::StopCapture);

  state.scheduleNextFrameCapture(10);
  EXPECT_EQ(state.getNextAction(10),
            dxmt::CaptureState::NextAction::StartCapture);
  EXPECT_EQ(state.getNextAction(11),
            dxmt::CaptureState::NextAction::StopCapture);
}

TEST(CaptureState, KeepsTheFirstPendingScheduleAndSupportsFrameZero) {
  ScopedEnvironmentVariable capture_enabled("MTL_CAPTURE_ENABLED", "1");
  dxmt::CaptureState state;

  state.scheduleNextFrameCapture();
  state.scheduleNextFrameCapture(4);
  EXPECT_FALSE(state.shouldCaptureNextFrame());
  EXPECT_EQ(state.getNextAction(0),
            dxmt::CaptureState::NextAction::StartCapture);
  EXPECT_EQ(state.getNextAction(1),
            dxmt::CaptureState::NextAction::StopCapture);

  state.scheduleNextFrameCapture(8);
  state.scheduleNextFrameCapture(9);
  EXPECT_EQ(state.getNextAction(8),
            dxmt::CaptureState::NextAction::StartCapture);
  EXPECT_EQ(state.getNextAction(9),
            dxmt::CaptureState::NextAction::StopCapture);
}

TEST(CaptureState, StartsOnTheFirstObservedFrameAfterASkippedTarget) {
  ScopedEnvironmentVariable capture_enabled("MTL_CAPTURE_ENABLED", "1");
  dxmt::CaptureState state;

  state.scheduleNextFrameCapture(5);
  EXPECT_EQ(state.getNextAction(7),
            dxmt::CaptureState::NextAction::StartCapture);
  EXPECT_EQ(state.getNextAction(7), dxmt::CaptureState::NextAction::Nothing);
  EXPECT_EQ(state.getNextAction(8),
            dxmt::CaptureState::NextAction::StopCapture);
}

} // namespace
