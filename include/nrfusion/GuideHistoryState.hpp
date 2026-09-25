#pragma once

#include "nrfusion/FrameContract.hpp"

namespace nrfusion {

struct GuideHistoryState {
    MotionSource motionSource = MotionSource::Zero;
    ResourceReliability motionReliability =
        ResourceReliability::Unknown;
    ResourceProvenance depthProvenance =
        ResourceProvenance::Unknown;
    ResourceReliability depthReliability =
        ResourceReliability::Unknown;
    ResourceProvenance exposureProvenance =
        ResourceProvenance::Unknown;
    ResourceReliability exposureReliability =
        ResourceReliability::Unknown;
    bool resetRequested = false;

    constexpr bool SamePersistentGuides(
        const GuideHistoryState& other) const noexcept {
        return motionSource == other.motionSource &&
               motionReliability == other.motionReliability &&
               depthProvenance == other.depthProvenance &&
               depthReliability == other.depthReliability &&
               exposureProvenance == other.exposureProvenance &&
               exposureReliability == other.exposureReliability;
    }
};

constexpr ResourceReliability ReliableFact(
    bool present,
    bool reliable) noexcept {
    if (!present)
        return ResourceReliability::Unknown;
    return reliable
        ? ResourceReliability::Reliable
        : ResourceReliability::Unreliable;
}

constexpr GuideHistoryState DescribeGuideHistory(
    const FrameContext& frame,
    MotionSource selectedMotion) noexcept {
    GuideHistoryState state{};
    state.motionSource = selectedMotion;

    const bool motionPresent =
        frame.motionVectors.Valid() &&
        frame.EffectiveMotionSource() == selectedMotion;
    state.motionReliability = ReliableFact(
        motionPresent,
        motionPresent && frame.MotionReliable(selectedMotion));

    if (frame.depth.Valid()) {
        state.depthProvenance = frame.depth.provenance;
    }
    state.depthReliability = ReliableFact(
        frame.depth.Valid(),
        frame.DepthReliable());

    if (frame.exposure.Valid()) {
        state.exposureProvenance = frame.exposure.provenance;
    }
    state.exposureReliability = ReliableFact(
        frame.exposure.Valid(),
        frame.ExposureReliable());

    state.resetRequested =
        frame.cameraCut || frame.resetHistory;
    return state;
}

} // namespace nrfusion
