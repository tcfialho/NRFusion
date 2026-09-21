#include "nrfusion/NrDeferredRetirementQueue.hpp"

namespace nrfusion {

bool NrDeferredRetirementQueue::Park(
    void*& object, NrRetiredObjectKind kind, std::uint32_t delay) noexcept {
    if (object == nullptr || delay == 0) return false;

    for (auto& entry : entries_) {
        if (entry.occupied) continue;
        entry.retired = {object, kind};
        entry.framesLeft = delay;
        entry.occupied = true;
        object = nullptr;
        ++size_;
        return true;
    }
    return false;
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
