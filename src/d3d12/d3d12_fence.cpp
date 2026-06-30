#include "d3d12_fence.hpp"

#include "com/com_guid.hpp"
#include "com/com_object.hpp"
#include "com/com_private_data.hpp"
#include "log/log.hpp"
#include "thread.hpp"
#include "util_env.hpp"
#include "util_string.hpp"
#include "util_win32_compat.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace dxmt::d3d12 {
namespace {

class CompletionCallbackRunner {
public:
  CompletionCallbackRunner()
      : worker_([this]() { WorkerThread(); }) {}

  ~CompletionCallbackRunner() {
    {
      std::lock_guard lock(mutex_);
      stopped_ = true;
    }
    cond_.notify_all();
    if (worker_.joinable())
      worker_.join();
  }

  void Enqueue(std::vector<std::function<void()>> callbacks) {
    if (callbacks.empty())
      return;

    {
      std::lock_guard lock(mutex_);
      for (auto &callback : callbacks)
        callbacks_.push_back(std::move(callback));
    }
    cond_.notify_one();
  }

private:
  void WorkerThread() {
    for (;;) {
      std::function<void()> callback;
      {
        std::unique_lock lock(mutex_);
        cond_.wait(lock, [this]() { return stopped_ || !callbacks_.empty(); });
        if (stopped_ && callbacks_.empty())
          return;

        callback = std::move(callbacks_.front());
        callbacks_.pop_front();
      }

      callback();
    }
  }

  dxmt::mutex mutex_;
  dxmt::condition_variable cond_;
  std::deque<std::function<void()>> callbacks_;
  bool stopped_ = false;
  dxmt::thread worker_;
};

static CompletionCallbackRunner &
GetCompletionCallbackRunner() {
  static CompletionCallbackRunner runner;
  return runner;
}

static void
RunCompletionCallbacksAsync(std::vector<std::function<void()>> callbacks) {
  GetCompletionCallbackRunner().Enqueue(std::move(callbacks));
}

static bool
D3D12FenceDiagEnabled() {
  static const bool enabled = []() {
    auto value = env::getEnvVar("DXMT_DIAG_D3D12_FENCE");
    if (value.empty())
      value = env::getEnvVar("DXMT_DIAG_COMMAND_QUEUE");
    return value == "1" || value == "true" || value == "yes" ||
           value == "trace";
  }();
  return enabled;
}

static bool
D3D12FenceDiagShouldLog(std::atomic<uint32_t> &counter) {
  if (!D3D12FenceDiagEnabled())
    return false;
  counter.fetch_add(1, std::memory_order_relaxed);
  return true;
}

using D3D12FenceDiagClock = std::chrono::high_resolution_clock;

static double
D3D12FenceDiagDurationMs(D3D12FenceDiagClock::duration duration) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration).count();
}

class FenceImpl final : public ComObjectWithInitialRef<ID3D12Fence>, public Fence {
public:
  FenceImpl(IMTLD3D12Device *device, UINT64 initial_value, D3D12_FENCE_FLAGS flags)
      : device_(device), event_(device->GetMTLDevice().newSharedEvent()), flags_(flags),
        completed_value_(initial_value), has_manual_completed_value_(false),
        last_signal_was_cpu_(false) {
    event_.signalValue(initial_value);
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: CreateFence"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " initial=", initial_value,
           " flags=", flags,
           " sharedEvent=", static_cast<uintptr_t>(event_.handle));
    }
  }

  ~FenceImpl() override {
    std::vector<HANDLE> events_to_signal;
    std::vector<std::function<void()>> callbacks_to_run;
    {
      std::lock_guard lock(mutex_);
      completed_value_ = UINT64_MAX;
      has_manual_completed_value_ = true;
      for (const auto &pending : pending_events_)
        events_to_signal.push_back(pending.event);
      pending_events_.clear();
      for (auto &pending : pending_callbacks_) {
        callbacks_to_run.push_back(std::move(pending.callback));
      }
      pending_callbacks_.clear();
    }
    for (HANDLE event : events_to_signal)
      SetEvent(event);
    for (auto &callback : callbacks_to_run)
      callback();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12Fence)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Fence), riid))
      WARN("D3D12Fence: unknown interface query ", str::format(riid));

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *data_size, void *data) override {
    return private_data_.getData(guid, data_size, data);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT data_size, const void *data) override {
    return private_data_.setData(guid, data_size, data);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *data) override {
    return private_data_.setInterface(guid, data);
  }

  HRESULT STDMETHODCALLTYPE SetName(const WCHAR *name) override {
    name_ = name ? str::fromws(name) : std::string();
    return private_data_.setName(name);
  }

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override {
    return device_->QueryInterface(riid, device);
  }

  UINT64 STDMETHODCALLTYPE GetCompletedValue() override {
    std::lock_guard lock(mutex_);
    const UINT64 value = GetCompletedValueLocked();
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: GetCompletedValue"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", value,
           " manual=", has_manual_completed_value_,
           " pendingEvents=", pending_events_.size(),
           " pendingCallbacks=", pending_callbacks_.size());
    }
    return value;
  }

  UINT64 GetCompletedValue() const override {
    std::lock_guard lock(mutex_);
    const UINT64 value = GetCompletedValueLocked();
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: GetCompletedValue internal"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", value,
           " manual=", has_manual_completed_value_,
           " pendingEvents=", pending_events_.size(),
           " pendingCallbacks=", pending_callbacks_.size());
    }
    return value;
  }

  HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 value, HANDLE event) override {
    const auto register_time = D3D12FenceDiagClock::now();
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: SetEventOnCompletion enter"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " target=", value,
           " event=", reinterpret_cast<uintptr_t>(event));
    }
    if (!event) {
      const auto wait_begin_time = D3D12FenceDiagClock::now();
      while (true) {
        {
          std::lock_guard lock(mutex_);
          if (GetCompletedValueLocked() >= value) {
            const auto wait_end_time = D3D12FenceDiagClock::now();
            static std::atomic<uint32_t> wait_log_count = 0;
            if (D3D12FenceDiagShouldLog(wait_log_count)) {
              WARN_FILE_ONLY("D3D12 fence diagnostic: SetEventOnCompletion sync wait"
                   " fence=", reinterpret_cast<uintptr_t>(this),
                   " target=", value,
                   " waitMs=", D3D12FenceDiagDurationMs(wait_end_time - wait_begin_time));
            }
            return S_OK;
          }
        }
        dxmt::this_thread::yield();
      }
    }

    bool signal_now = false;
    {
      std::lock_guard lock(mutex_);
      if (GetCompletedValueLocked() >= value) {
        signal_now = true;
      } else {
        pending_events_.push_back({value, event, register_time});
      }
    }

    if (signal_now)
      SetEvent(event);

    static std::atomic<uint32_t> result_log_count = 0;
    if (D3D12FenceDiagShouldLog(result_log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: SetEventOnCompletion result"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " target=", value,
           " signalNow=", signal_now,
           " elapsedMs=", D3D12FenceDiagDurationMs(D3D12FenceDiagClock::now() - register_time));
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Signal(UINT64 value) override {
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: Signal CPU"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", value);
    }
    std::vector<HANDLE> events_to_signal;
    std::vector<std::function<void()>> callbacks_to_run;
    {
      std::lock_guard lock(mutex_);
      completed_value_ = value;
      has_manual_completed_value_ = true;
      last_signal_was_cpu_ = true;
      pending_gpu_signals_.clear();
      collectCompletedEventsLocked(events_to_signal, callbacks_to_run);
    }
    event_.signalValue(value);
    for (HANDLE event : events_to_signal)
      SetEvent(event);
    RunCompletionCallbacksAsync(std::move(callbacks_to_run));
    return S_OK;
  }

  WMT::Reference<WMT::SharedEvent> GetSharedEvent() const override { return event_; }

  void AddRefPrivate() override {
    ComObjectWithInitialRef<ID3D12Fence>::AddRefPrivate();
  }

  void ReleasePrivate() override {
    ComObjectWithInitialRef<ID3D12Fence>::ReleasePrivate();
  }

  void SetCompletedValue(UINT64 value) override {
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: SetCompletedValue queue-complete"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", value);
    }
    std::vector<HANDLE> events_to_signal;
    std::vector<std::function<void()>> callbacks_to_run;
    {
      std::lock_guard lock(mutex_);
      completed_value_ = value;
      has_manual_completed_value_ = true;
      last_signal_was_cpu_ = false;
      pruneGpuSignalsLocked(value);
      collectCompletedEventsLocked(events_to_signal, callbacks_to_run);
    }
    event_.signalValue(value);
    for (HANDLE event : events_to_signal)
      SetEvent(event);
    RunCompletionCallbacksAsync(std::move(callbacks_to_run));
  }

  void SignalFromQueue(UINT64 value) override {
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: SignalFromQueue"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", value);
    }
    Signal(value);
  }

  void AddCompletionCallback(UINT64 value, std::function<void()> callback) override {
    const auto register_time = D3D12FenceDiagClock::now();
    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: AddCompletionCallback"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " target=", value);
    }
    bool run_now = false;
    {
      std::lock_guard lock(mutex_);
      if (GetCompletedValueLocked() >= value) {
        run_now = true;
      } else {
        pending_callbacks_.push_back({value, std::move(callback), register_time});
      }
    }

    if (run_now) {
      static std::atomic<uint32_t> run_now_log_count = 0;
      if (D3D12FenceDiagShouldLog(run_now_log_count)) {
        WARN_FILE_ONLY("D3D12 fence diagnostic: AddCompletionCallback run-now"
             " fence=", reinterpret_cast<uintptr_t>(this),
             " target=", value,
             " elapsedMs=", D3D12FenceDiagDurationMs(D3D12FenceDiagClock::now() - register_time));
      }
      std::vector<std::function<void()>> callbacks;
      callbacks.push_back(std::move(callback));
      RunCompletionCallbacksAsync(std::move(callbacks));
      return;
    }
  }

  bool HasReached(UINT64 value) const override {
    return MTLSharedEvent_signaledValue(event_.handle) >= value;
  }

  void RegisterQueueSignal(const FenceGpuSignal &signal) override {
    std::lock_guard lock(mutex_);
    if (flags_ & D3D12_FENCE_FLAG_SHARED)
      return;

    const UINT64 completed_value = GetCompletedValueLocked();
    pruneGpuSignalsLocked(completed_value);
    if (completed_value >= signal.signal_value)
      return;
    last_signal_was_cpu_ = false;

    auto it = std::lower_bound(
        pending_gpu_signals_.begin(), pending_gpu_signals_.end(),
        signal.signal_value,
        [](const FenceGpuSignal &pending, UINT64 value) {
          return pending.signal_value < value;
        });
    if (it != pending_gpu_signals_.end() &&
        it->signal_value == signal.signal_value) {
      *it = signal;
    } else {
      pending_gpu_signals_.insert(it, signal);
    }

    static std::atomic<uint32_t> log_count = 0;
    if (D3D12FenceDiagShouldLog(log_count)) {
      WARN_FILE_ONLY("D3D12 fence diagnostic: RegisterQueueSignal"
           " fence=", reinterpret_cast<uintptr_t>(this),
           " value=", signal.signal_value,
           " queue=", signal.queue,
           " queueType=", signal.queue_type,
           " dxmtChunk=", signal.dxmt_chunk,
           " chunkEvent=", signal.chunk_event,
           " frame=", signal.frame,
           " pendingGpuSignals=", pending_gpu_signals_.size());
    }
  }

  FenceGpuWaitStatus TryResolveGpuWait(UINT64 value, FenceGpuSignal &signal) const override {
    std::lock_guard lock(mutex_);
    if (GetCompletedValueLocked() >= value)
      return FenceGpuWaitStatus::Resolved;
    if (last_signal_was_cpu_)
      return FenceGpuWaitStatus::CpuSignal;
    if (flags_ & D3D12_FENCE_FLAG_SHARED)
      return FenceGpuWaitStatus::Shared;

    auto it = std::lower_bound(
        pending_gpu_signals_.begin(), pending_gpu_signals_.end(), value,
        [](const FenceGpuSignal &pending, UINT64 wait_value) {
          return pending.signal_value < wait_value;
        });
    if (it == pending_gpu_signals_.end())
      return pending_gpu_signals_.empty() ? FenceGpuWaitStatus::Unknown
                                          : FenceGpuWaitStatus::Rewind;

    signal = *it;
    return FenceGpuWaitStatus::Resolved;
  }

private:
  struct PendingEvent {
    UINT64 value;
    HANDLE event;
    D3D12FenceDiagClock::time_point registered_time;
  };

  struct PendingCallback {
    UINT64 value;
    std::function<void()> callback;
    D3D12FenceDiagClock::time_point registered_time;
  };

  UINT64 GetCompletedValueLocked() const {
    if (has_manual_completed_value_)
      return completed_value_;
    return MTLSharedEvent_signaledValue(event_.handle);
  }

  void collectCompletedEventsLocked(
      std::vector<HANDLE> &events,
      std::vector<std::function<void()>> &callbacks) {
    const UINT64 completed_value = GetCompletedValueLocked();
    const auto completed_time = D3D12FenceDiagClock::now();
    auto it = std::remove_if(pending_events_.begin(), pending_events_.end(),
                             [&](const PendingEvent &pending) {
                               if (completed_value < pending.value)
                                 return false;
                               events.push_back(pending.event);
                               static std::atomic<uint32_t> event_log_count = 0;
                               if (D3D12FenceDiagShouldLog(event_log_count)) {
                                 WARN_FILE_ONLY("D3D12 fence diagnostic: CompletePendingEvent"
                                      " fence=", reinterpret_cast<uintptr_t>(this),
                                      " target=", pending.value,
                                      " completed=", completed_value,
                                      " ageMs=", D3D12FenceDiagDurationMs(completed_time - pending.registered_time));
                               }
                               return true;
                             });
    pending_events_.erase(it, pending_events_.end());
    auto callback_it = std::remove_if(
        pending_callbacks_.begin(), pending_callbacks_.end(),
        [&](PendingCallback &pending) {
          if (completed_value < pending.value)
            return false;
          callbacks.push_back(std::move(pending.callback));
          static std::atomic<uint32_t> callback_log_count = 0;
          if (D3D12FenceDiagShouldLog(callback_log_count)) {
            WARN_FILE_ONLY("D3D12 fence diagnostic: CompletePendingCallback"
                 " fence=", reinterpret_cast<uintptr_t>(this),
                 " target=", pending.value,
                 " completed=", completed_value,
                 " ageMs=", D3D12FenceDiagDurationMs(completed_time - pending.registered_time));
          }
          return true;
        });
    pending_callbacks_.erase(callback_it, pending_callbacks_.end());
  }

  void pruneGpuSignalsLocked(UINT64 completed_value) {
    auto it = std::remove_if(
        pending_gpu_signals_.begin(), pending_gpu_signals_.end(),
        [completed_value](const FenceGpuSignal &signal) {
          return signal.signal_value <= completed_value;
        });
    pending_gpu_signals_.erase(it, pending_gpu_signals_.end());
  }

  Com<IMTLD3D12Device> device_;
  ComPrivateData private_data_;
  WMT::Reference<WMT::SharedEvent> event_;
  D3D12_FENCE_FLAGS flags_;
  mutable std::mutex mutex_;
  std::vector<PendingEvent> pending_events_;
  std::vector<PendingCallback> pending_callbacks_;
  std::vector<FenceGpuSignal> pending_gpu_signals_;
  UINT64 completed_value_;
  bool has_manual_completed_value_;
  bool last_signal_was_cpu_;
  std::string name_;
};

} // namespace

Com<ID3D12Fence>
CreateFence(IMTLD3D12Device *device, UINT64 initial_value, D3D12_FENCE_FLAGS flags) {
  return Com<ID3D12Fence>::transfer(new FenceImpl(device, initial_value, flags));
}

} // namespace dxmt::d3d12
