#pragma once

#include "d3d12_device.hpp"
#include "Metal.hpp"
#include <functional>
#include <d3d12.h>

namespace dxmt::d3d12 {

class Fence {
public:
  virtual ~Fence() = default;

  virtual WMT::Reference<WMT::SharedEvent> GetSharedEvent() const = 0;
  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
  virtual UINT64 GetCompletedValue() const = 0;
  virtual void SetCompletedValue(UINT64 value) = 0;
  virtual void SignalFromQueue(UINT64 value) = 0;
  virtual void AddCompletionCallback(UINT64 value, std::function<void()> callback) = 0;
  virtual bool HasReached(UINT64 value) const = 0;
};

Com<ID3D12Fence>
CreateFence(IMTLD3D12Device *device, UINT64 initial_value, D3D12_FENCE_FLAGS flags);

} // namespace dxmt::d3d12
