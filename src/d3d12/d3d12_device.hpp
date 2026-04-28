#pragma once

#include "d3d12_interfaces.hpp"
#include "dxmt_device.hpp"
#include "com/com_pointer.hpp"
#include <memory>

namespace dxmt::d3d12 {

class Device;

Com<IMTLD3D12Device> CreateD3D12Device(std::unique_ptr<dxmt::Device> &&device, IMTLDXGIAdapter *adapter);

} // namespace dxmt::d3d12
