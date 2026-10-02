#pragma once
#include "Replay.hpp"
#include "nrfusion/NrSwinCaptureContract.hpp"

namespace nrreplay {
struct SwinPacket {
    Bytes image;
    std::array<std::uint8_t, 88> parameters{};
    std::array<Bytes, 2> before, expected;
    std::array<std::uint64_t, 5> offsets{};
};
SwinPacket LoadSwinPacket(const std::filesystem::path&);
bool ReplaySwin(const SwinPacket&, const std::filesystem::path&, const std::filesystem::path& custom = {});
} // namespace nrreplay
