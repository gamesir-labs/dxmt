#pragma once

#include "d3d12_device.hpp"
#include <d3d12.h>
#include <memory>
#include <mutex>

namespace dxmt::d3d12 {

class CommandQueue;

// Serializes direct access to the device-wide DXMT command queue with D3D12
// submission workers. The opaque shared owner keeps the mutex alive for the
// full critical section. Callers must release this guard before waiting for a
// DXMT CPU fence so the submission worker can continue making progress.
struct DxmtQueueSubmissionGuard {
  std::shared_ptr<void> owner;
  std::unique_lock<std::mutex> lock;
};

DxmtQueueSubmissionGuard
AcquireDxmtQueueSubmissionGuard(IMTLD3D12Device *device);

HRESULT
CreateCommandQueue(IMTLD3D12Device *device, const D3D12_COMMAND_QUEUE_DESC *desc,
                   REFIID riid, void **command_queue);

} // namespace dxmt::d3d12
