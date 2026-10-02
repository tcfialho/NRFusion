#include "nrfusion/NrDeferredRetirementQueue.hpp"

#include <limits>

namespace nrfusion {

bool NrDeferredRetirementQueue::Park(
    void*& object, NrRetiredObjectKind kind,
    std::uint32_t delay, std::uint64_t logicalBytes) noexcept {
    if (object == nullptr || delay == 0) return false;
    if (kind != NrRetiredObjectKind::Feature && kind != NrRetiredObjectKind::Resource)
        return false;

    for (auto& entry : entries_) {
        if (entry.occupied) continue;
        entry.retired = {object, kind, logicalBytes};
        entry.framesLeft = delay;
        entry.occupied = true;
        object = nullptr;
        ++size_;
        return true;
    }
    return false;
}

NrDeferredRetirementAccounting
NrDeferredRetirementQueue::ResourceAccounting() const noexcept {
    NrDeferredRetirementAccounting result{};
    for (const auto& entry : entries_) {
        if (!entry.occupied ||
            entry.retired.kind != NrRetiredObjectKind::Resource)
            continue;
        ++result.resourceCount;
        if (entry.retired.logicalBytes == 0) {
            result.logicalBytesExact = false;
            continue;
        }
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        if (result.logicalBytes > max - entry.retired.logicalBytes) {
            result.logicalBytes = max;
            result.logicalBytesExact = false;
            continue;
        }
        result.logicalBytes += entry.retired.logicalBytes;
    }
    return result;
}

void NrDeferredRetirementQueue::Tick(void* context, ReleaseFn release) noexcept {
    if (release == nullptr) return;

    for (auto& entry : entries_) {
        if (!entry.occupied) continue;
        if (entry.framesLeft > 1) {
            --entry.framesLeft;
            continue;
        }

        release(context, entry.retired);
        entry = {};
        --size_;
    }
}

void NrDeferredRetirementQueue::DrainAfterIdle(void* context, ReleaseFn release) noexcept {
    if (release == nullptr) return;

    for (auto& entry : entries_) {
        if (!entry.occupied) continue;
        release(context, entry.retired);
        entry = {};
    }
    size_ = 0;
}

} // namespace nrfusion
