#pragma once

#include "nrfusion/FrameContract.hpp"

namespace nrfusion {

struct GuideHistoryState {
    std::uint64_t configurationGeneration = 0;
    MotionSource motionSource = MotionSource::Zero;
    ResourceProvenance motionProvenance =
        ResourceProvenance::Unknown;
    ResourceReliability motionReliability =
        ResourceReliability::Unknown;
    Resolution motionResolution{};
    ResourceFormat motionFormat = ResourceFormat::Unknown;

    ResourceProvenance depthProvenance =
        ResourceProvenance::Unknown;
    ResourceReliability depthReliability =
        ResourceReliability::Unknown;
    Resolution depthResolution{};
    ResourceFormat depthFormat = ResourceFormat::Unknown;

    ResourceProvenance exposureProvenance =
        ResourceProvenance::Unknown;
    ResourceReliability exposureReliability =
        ResourceReliability::Unknown;
    Resolution exposureResolution{};
    ResourceFormat exposureFormat = ResourceFormat::Unknown;

    bool resetRequested = false;

    constexpr bool SamePersistentGuides(
        const GuideHistoryState& other) const noexcept {
        return configurationGeneration ==
                   other.configurationGeneration &&
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
        return reliable
            ? ResourceReliability::Reliable
            : ResourceReliability::Unreliable;
    return reliable
        ? ResourceReliability::Reliable
        : ResourceReliability::Unknown;
}

constexpr GuideHistoryState DescribeGuideHistory(
    const FrameContext& frame,
    MotionSource selectedMotion) noexcept {
    GuideHistoryState state{};
    state.configurationGeneration =
        frame.configurationGeneration;
    state.resetRequested =
        frame.cameraCut || frame.resetHistory;

    const bool motionUsable =
        GuideEvidenceUsable(frame.motionVectors, frame.frameId);
    state.motionSource =
        state.resetRequested && motionUsable
            ? frame.EffectiveMotionSource()
            : selectedMotion;
    if (motionUsable) {
        state.motionProvenance =
            frame.motionVectors.provenance;
        state.motionResolution =
            frame.motionVectors.resolution;
        state.motionFormat =
            frame.motionVectors.format;
    }
    state.motionReliability = GuideReliability(
        frame.motionVectors,
        frame.frameId,
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

} // namespace nrfusion
