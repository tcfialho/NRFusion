#include "nrfusion/D3D11BridgeSlotTracker.hpp"

namespace nrfusion {

std::optional<D3D11BridgeSlotLease> D3D11BridgeSlotTracker::Acquire(
    std::uint64_t workId, std::uint64_t completedFence) noexcept {
    if (workId == 0 || Find(workId)) return std::nullopt;

    for (std::uint32_t offset = 0; offset < kCapacity; ++offset) {
        const std::uint32_t slot = (next_ + offset) % kCapacity;
        Entry& entry = entries_[slot];
        const bool retired = entry.occupied && entry.fenceValue != 0 &&
            entry.fenceValue <= completedFence;
        if (entry.occupied && !retired) continue;

        entry = {workId, 0, true};
        next_ = (slot + 1) % kCapacity;
        return D3D11BridgeSlotLease{slot, workId};
    }
    return std::nullopt;
}

bool D3D11BridgeSlotTracker::MarkSubmitted(
    const D3D11BridgeSlotLease& lease, std::uint64_t fenceValue) noexcept {
    if (lease.slot >= kCapacity || lease.workId == 0 || fenceValue == 0)
        return false;
    Entry& entry = entries_[lease.slot];
    if (!entry.occupied || entry.workId != lease.workId || entry.fenceValue != 0)
        return false;
    entry.fenceValue = fenceValue;
    return true;
}

bool D3D11BridgeSlotTracker::Release(
    const D3D11BridgeSlotLease& lease) noexcept {
    if (lease.slot >= kCapacity || lease.workId == 0) return false;
    Entry& entry = entries_[lease.slot];
    if (!entry.occupied || entry.workId != lease.workId || entry.fenceValue != 0)
        return false;
    entry = {};
    return true;
}

std::optional<std::uint32_t> D3D11BridgeSlotTracker::Find(
    std::uint64_t workId) const noexcept {
    if (workId == 0) return std::nullopt;
    for (std::uint32_t slot = 0; slot < kCapacity; ++slot) {
        if (entries_[slot].occupied && entries_[slot].workId == workId)
            return slot;
    }
    return std::nullopt;
}

bool D3D11BridgeSlotTracker::AllRetired(
    std::uint64_t completedFence) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.occupied &&
            (entry.fenceValue == 0 || entry.fenceValue > completedFence))
            return false;
    }
    return true;
}

void D3D11BridgeSlotTracker::Reset() noexcept {
    for (Entry& entry : entries_) entry = {};
    next_ = 0;
}

} // namespace nrfusion
