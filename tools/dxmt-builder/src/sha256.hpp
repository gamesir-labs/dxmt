#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace dxmt::builder {

std::string Sha256(std::string_view input);
std::string Sha256File(const std::filesystem::path &path);

} // namespace dxmt::builder
