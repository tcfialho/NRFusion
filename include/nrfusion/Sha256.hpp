#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <cstdint>

namespace nrfusion {

std::string Sha256Hex(std::span<const std::uint8_t> bytes);
std::optional<std::string> Sha256File(const std::filesystem::path& path);
bool Sha256FileEquals(const std::filesystem::path& path, const std::string& expectedHex);

} // namespace nrfusion
