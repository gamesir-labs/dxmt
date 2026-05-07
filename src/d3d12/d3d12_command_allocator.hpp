#pragma once

#include "d3d12_device.hpp"
#include <d3d12.h>

namespace dxmt::d3d12 {

class CommandAllocator {
public:
  virtual ~CommandAllocator() = default;

  virtual D3D12_COMMAND_LIST_TYPE GetCommandListType() const = 0;
  virtual bool BeginCommandListRecording(void *command_list) = 0;
  virtual void EndCommandListRecording(void *command_list) = 0;
  virtual UINT64 MarkCommandListSubmitted() = 0;
  virtual void CompleteCommandListSubmission(UINT64 serial) = 0;
};

class CommandAllocatorObject : public ID3D12CommandAllocator,
                               public CommandAllocator {
public:
  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
};

Com<ID3D12CommandAllocator>
CreateCommandAllocator(IMTLD3D12Device *device, D3D12_COMMAND_LIST_TYPE type);

} // namespace dxmt::d3d12
