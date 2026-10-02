#pragma once

#include <cstdint>
#include <limits>

namespace nrfusion {

struct OpenGlCarrierSyncIdentity {
    std::uint64_t workId = 0;
    std::uint32_t slotIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::uint64_t inputSignalValue = 0;
    std::uint64_t outputSignalValue = 0;
    std::uint64_t releaseSignalValue = 0;

    constexpr bool Valid(std::uint32_t slotCount) const noexcept {
        return workId != 0 &&
               slotIndex < slotCount &&
               inputSignalValue != 0 &&
               inputSignalValue < outputSignalValue &&
               outputSignalValue < releaseSignalValue;
    }
};

constexpr bool ReserveOpenGlCarrierSyncIdentity(
    std::uint64_t workId,
    std::uint32_t slotIndex,
    std::uint32_t slotCount,
    std::uint64_t& nextFenceValue,
    OpenGlCarrierSyncIdentity& out) noexcept {
    constexpr std::uint64_t maxValue =
        std::numeric_limits<std::uint64_t>::max();
    if (workId == 0 ||
        slotCount == 0 ||
        slotIndex >= slotCount ||
        nextFenceValue == 0 ||
        nextFenceValue > maxValue - 2) {
        return false;
    }

    out.workId = workId;
    out.slotIndex = slotIndex;
    out.inputSignalValue = nextFenceValue;
    out.outputSignalValue = nextFenceValue + 1;
    out.releaseSignalValue = nextFenceValue + 2;
    nextFenceValue =
        out.releaseSignalValue == maxValue
        ? 0
        : out.releaseSignalValue + 1;
    return true;
}

} // namespace nrfusion
