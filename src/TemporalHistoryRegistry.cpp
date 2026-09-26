#include "nrfusion/TemporalHistoryRegistry.hpp"

#include <limits>
#include <stdexcept>

namespace nrfusion {

std::size_t TemporalHistoryRegistry::RegistryKeyHash::operator()(const RegistryKey& key) const noexcept {
    const auto a = static_cast<std::size_t>(key.feature ^ (key.feature >> 33));
    const auto b = static_cast<std::size_t>(key.view ^ (key.view >> 29));
    return a ^ (b + static_cast<std::size_t>(0x9e3779b9u) + (a << 6) + (a >> 2));
}

bool TemporalHistoryRegistry::SameShape(const ViewDescriptor& a, const ViewDescriptor& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height &&
           a.outputWidth == b.outputWidth && a.outputHeight == b.outputHeight;
}

std::uint64_t TemporalHistoryRegistry::AllocateHistoryId() {
    if (nextHistoryId_ == 0) throw std::overflow_error("NRFusion history ID space exhausted");
    const std::uint64_t id = nextHistoryId_;
    if (nextHistoryId_ == std::numeric_limits<std::uint64_t>::max()) nextHistoryId_ = 0;
    else ++nextHistoryId_;
    return id;
}

HistoryLease TemporalHistoryRegistry::Acquire(
    const ViewDescriptor& view,
    std::uint64_t frameNumber,
    const GuideHistoryState& guides) {
    // featureKey==0 cannot safely share history across unknown callers: issue a fresh identity.
    if (view.featureKey == 0) return {AllocateHistoryId(), true};

    const RegistryKey key{view.featureKey, view.viewKey};
    auto [it, inserted] = entries_.try_emplace(key);
    Entry& e = it->second;
    if (inserted) {
        e.descriptor = view;
        e.historyId = AllocateHistoryId();
        e.lastSeenFrame = frameNumber;
        e.lastResetFrame = frameNumber;
        e.guides = guides;
        e.guides.resetRequested = false;
        return {e.historyId, true};
    }

    if (frameNumber < e.lastSeenFrame) {
        // A late/out-of-order request must not roll the live view's history backward. Give it an
        // isolated reset identity and leave the current timeline untouched.
        return {AllocateHistoryId(), true};
    }

    const bool shapeChanged =
        !SameShape(e.descriptor, view);
    // A transient cut/reset invalidates history but must not overwrite the
    // persistent guide signature with the temporary Zero selection.
    const bool guideChanged =
        !guides.resetRequested &&
        !e.guides.SamePersistentGuides(guides);
    const bool resetRequired =
        guides.resetRequested ||
        shapeChanged ||
        guideChanged;
    if (resetRequired) {
        e.descriptor = view;
        if (e.lastResetFrame != frameNumber) {
            e.historyId = AllocateHistoryId();
            e.lastResetFrame = frameNumber;
        }
    }
    if (!guides.resetRequested) {
        e.guides = guides;
        e.guides.resetRequested = false;
    }
    e.lastSeenFrame = frameNumber;
    return {e.historyId, resetRequired};
}

void TemporalHistoryRegistry::InvalidateFeature(std::uint64_t featureKey) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.feature == featureKey) it = entries_.erase(it);
        else ++it;
    }
}

void TemporalHistoryRegistry::Prune(std::uint64_t frameNumber, std::uint64_t maxIdleFrames) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        const auto age = frameNumber >= it->second.lastSeenFrame ? frameNumber - it->second.lastSeenFrame : 0;
        if (age > maxIdleFrames) it = entries_.erase(it);
        else ++it;
    }
}

void TemporalHistoryRegistry::Clear() {
    entries_.clear();
}

} // namespace nrfusion
