#include "nrfusion/FusionRuntime.hpp"

#include <limits>

namespace nrfusion {

bool FusionRuntime::CanBeginConfigurationEpoch() const noexcept {
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    return autoExecutionGeneration_ != max && autoPrecisionGeneration_ != max;
}

HistoryLease FusionRuntime::AcquireHistory(
    const ViewDescriptor& view,
    const FrameContext& frame,
    MotionSource selectedMotion) {
    return histories_.Acquire(
        view, frame.frameId,
        DescribeGuideHistory(frame, selectedMotion));
}

HistoryLease FusionRuntime::AcquireHistory(
    const ViewDescriptor& view,
    std::uint64_t frameNumber) {
    return histories_.Acquire(view, frameNumber);
}

bool FusionRuntime::UpdateAutoGuideHistory(
    const FrameContext& frame,
    MotionSource selectedMotion) {
    ViewDescriptor view{};
    view.featureKey = (std::numeric_limits<std::uint64_t>::max)();
    view.viewKey = frame.viewId;
    view.width = frame.renderResolution.width;
    view.height = frame.renderResolution.height;
    view.outputWidth = frame.outputResolution.width;
    view.outputHeight = frame.outputResolution.height;
    return histories_.Acquire(
        view, frame.frameId,
        DescribeGuideHistory(frame, selectedMotion)).resetRequired;
}

void FusionRuntime::BeginConfigurationEpoch(float initialScale) {
    autoExecutionGeneration_ = NextAutoConfigurationGeneration(
        autoExecutionGeneration_, "Auto execution generation");
    autoPrecisionGeneration_ = NextAutoConfigurationGeneration(
        autoPrecisionGeneration_, "Auto precision generation");
    ResetAutoAdaptiveState(initialScale);
    haveAutoDecision_ = false;
}

} // namespace nrfusion
