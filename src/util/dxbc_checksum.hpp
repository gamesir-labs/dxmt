/*
 * This file is part of DXMT.
 *
 * DXBC checksum finalization follows the MD5 variant used by the Direct3D
 * shader container format.
 */

#pragma once

#include <cstddef>

namespace dxmt {

bool ValidateDxbcChecksum(const void *data, std::size_t size);

} // namespace dxmt
