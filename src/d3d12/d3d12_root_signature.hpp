#pragma once

#include "com/com_pointer.hpp"
#include <d3d12.h>
#include <cstddef>
#include <span>

namespace dxmt::d3d12 {

class RootSignature;

Com<RootSignature> CreateRootSignatureFromBlob(std::span<const std::byte> blob);

} // namespace dxmt::d3d12
