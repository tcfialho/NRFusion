#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "nrfusion/FrameContract.hpp"

namespace nrfusion {

struct GuideHistoryState {
    std::uint64_t configurationGeneration = 0;
    MotionSource motionSource = MotionSource::Zero;
    ResourceProvenance motionProvenance = ResourceProvenance::Unknown;
    ResourceReliability motionReliability = ResourceReliability::Unknown;
    Resolution motionResolution{};
    ResourceFormat motionFormat = ResourceFormat::Unknown;

    ResourceProvenance depthProvenance = ResourceProvenance::Unknown;
    ResourceReliability depthReliability = ResourceReliability::Unknown;
    Resolution depthResolution{};
    ResourceFormat depthFormat = ResourceFormat::Unknown;

    ResourceProvenance exposureProvenance = ResourceProvenance::Unknown;
    ResourceReliability exposureReliability = ResourceReliability::Unknown;
    Resolution exposureResolution{};
    ResourceFormat exposureFormat = ResourceFormat::Unknown;
    bool resetRequested = false;

    constexpr bool SamePersistentGuides(
        const GuideHistoryState& other) const noexcept {
        return configurationGeneration == other.configurationGeneration &&
               motionSource == other.motionSource &&
               motionProvenance == other.motionProvenance &&
               motionReliability == other.motionReliability &&
               motionResolution == other.motionResolution &&
               motionFormat == other.motionFormat &&
               depthProvenance == other.depthProvenance &&
               depthReliability == other.depthReliability &&
               depthResolution == other.depthResolution &&
               depthFormat == other.depthFormat &&
               exposureProvenance == other.exposureProvenance &&
               exposureReliability == other.exposureReliability &&
               exposureResolution == other.exposureResolution &&
               exposureFormat == other.exposureFormat;
    }
};

constexpr bool GuideEvidenceUsable(
    const ResourceRef& resource,
    FrameId frameId) noexcept {
    return resource.Valid() &&
           resource.EvidenceWellFormed() &&
           resource.BelongsToFrame(frameId);
}

constexpr ResourceReliability GuideReliability(
    const ResourceRef& resource,
    FrameId frameId,
    bool reliable) noexcept {
    if (!GuideEvidenceUsable(resource, frameId))
        return ResourceReliability::Unknown;
    if (resource.reliability != ResourceReliability::Unknown)
        return reliable ? ResourceReliability::Reliable
                        : ResourceReliability::Unreliable;
    return reliable ? ResourceReliability::Reliable
                    : ResourceReliability::Unknown;
}

constexpr GuideHistoryState DescribeGuideHistory(
    const FrameContext& frame,
    MotionSource selectedMotion) noexcept {
    GuideHistoryState state{};
    state.configurationGeneration = frame.configurationGeneration;
    state.resetRequested = frame.cameraCut || frame.resetHistory;

    const bool motionUsable =
        GuideEvidenceUsable(frame.motionVectors, frame.frameId);
    state.motionSource =
        state.resetRequested && motionUsable
            ? frame.EffectiveMotionSource()
            : selectedMotion;
    if (motionUsable) {
        state.motionProvenance = frame.motionVectors.provenance;
        state.motionResolution = frame.motionVectors.resolution;
        state.motionFormat = frame.motionVectors.format;
    }
    state.motionReliability = GuideReliability(
        frame.motionVectors, frame.frameId,
        frame.MotionReliable(state.motionSource));

    const bool depthUsable =
        GuideEvidenceUsable(frame.depth, frame.frameId);
    if (depthUsable) {
        state.depthProvenance = frame.depth.provenance;
        state.depthResolution = frame.depth.resolution;
        state.depthFormat = frame.depth.format;
    }
    state.depthReliability = GuideReliability(
        frame.depth, frame.frameId, frame.DepthReliable());

    const bool exposureUsable =
        GuideEvidenceUsable(frame.exposure, frame.frameId);
    if (exposureUsable) {
        state.exposureProvenance = frame.exposure.provenance;
        state.exposureResolution = frame.exposure.resolution;
        state.exposureFormat = frame.exposure.format;
    }
    state.exposureReliability = GuideReliability(
        frame.exposure, frame.frameId, frame.ExposureReliable());
    return state;
}

struct ViewDescriptor {
    std::uint64_t featureKey = 0; // stable identity of the game's temporal feature
    std::uint64_t viewKey = 0;    // optional stable sub-view identity (split-screen/VR eye/etc.)
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
};

struct HistoryLease {
    std::uint64_t historyId = 0;
    bool resetRequired = true;
};

// Assigns an independent temporal-history identity to each game feature/view and requests a reset
// when that view changes shape. GPU resources remain owned by the host; historyId is the host key.
class TemporalHistoryRegistry {
public:
    HistoryLease Acquire(
        const ViewDescriptor& view,
        std::uint64_t frameNumber,
        const GuideHistoryState& guides);
    void InvalidateFeature(std::uint64_t featureKey);
    void Prune(std::uint64_t frameNumber, std::uint64_t maxIdleFrames = 600);
    void Clear();
    std::size_t Size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        ViewDescriptor descriptor{};
        std::uint64_t historyId = 0;
        std::uint64_t lastSeenFrame = 0;
        std::uint64_t lastResetFrame = 0;
        GuideHistoryState guides{};
    };

    struct RegistryKey {
        std::uint64_t feature = 0;
        std::uint64_t view = 0;
        bool operator==(const RegistryKey&) const = default;
    };
    struct RegistryKeyHash {
        std::size_t operator()(const RegistryKey& key) const noexcept;
    };

    static bool SameShape(const ViewDescriptor& a, const ViewDescriptor& b);
    std::uint64_t AllocateHistoryId();
    std::unordered_map<RegistryKey, Entry, RegistryKeyHash> entries_;
    std::uint64_t nextHistoryId_ = 1;
};

} // namespace nrfusion
