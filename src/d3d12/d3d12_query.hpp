#pragma once

#include "d3d12_device.hpp"
#include "com/com_pointer.hpp"
#include <d3d12.h>

namespace dxmt::d3d12 {

class QueryHeap;

Com<ID3D12QueryHeap>
CreateQueryHeap(IMTLD3D12Device *device, const D3D12_QUERY_HEAP_DESC *desc);

} // namespace dxmt::d3d12
