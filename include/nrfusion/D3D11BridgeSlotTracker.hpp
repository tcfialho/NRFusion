#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace nrfusion {

struct D3D11BridgeSlotLease {
    std::uint32_t slot = 0;
    std::uint64_t workId = 0;
};

class D3D11BridgeSlotTracker {
public:
    static constexpr std::uint32_t kCapacity = 3;

    std::optional<D3D11BridgeSlotLease> Acquire(
        std::uint64_t workId, std::uint64_t completedFence) noexcept;
    bool MarkSubmitted(
        const D3D11BridgeSlotLease& lease, std::uint64_t fenceValue) noexcept;
    bool Release(const D3D11BridgeSlotLease& lease) noexcept;
    std::optional<std::uint32_t> Find(std::uint64_t workId) const noexcept;
    bool AllRetired(std::uint64_t completedFence) const noexcept;
    void Reset() noexcept;

private:
    struct Entry {
        std::uint64_t workId = 0;
        std::uint64_t fenceValue = 0;
        bool occupied = false;
    };

    std::array<Entry, kCapacity> entries_{};
    std::uint32_t next_ = 0;
};

} // namespace nrfusion
