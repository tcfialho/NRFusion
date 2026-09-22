#include "nrfusion/D3D12CarrierExecutionPlan.hpp"

#include <cmath>

namespace nrfusion {
namespace {

D3D12NrSubrect FullRect(Resolution resolution) noexcept {
    return {0, 0, resolution.width, resolution.height};
}

bool ValidMotionScale(float value) noexcept {
    return std::isfinite(value);
}

} // namespace

D3D12CarrierExecutionPlanResult BuildD3D12CarrierExecutionPlan(
    const D3D12CarrierFrameResult& frame,
    const D3D12CarrierWork& work,
    const D3D12CarrierExecutionConfig& config) noexcept {
    D3D12CarrierExecutionPlanResult result{};

    if (!frame || frame.acquire.frame.api != GraphicsApi::D3D12 ||
        frame.session.decision.pipeline.api != GraphicsApi::D3D12) {
        result.failure = D3D12CarrierExecutionFailure::InvalidFrame;
        return result;
    }
    if (!work || work.submissionEpoch != work.ticket.id) {
        result.failure = D3D12CarrierExecutionFailure::InvalidWork;
        return result;
    }
    if (work.ticket.sourceFrame != frame.session.frameId) {
        result.failure = D3D12CarrierExecutionFailure::WorkFrameMismatch;
        return result;
    }
    if (work.ticket.configurationGeneration != frame.session.runtimeGeneration ||
        work.ticket.workingScale != frame.session.decision.workingScale) {
        result.failure = D3D12CarrierExecutionFailure::WorkGenerationMismatch;
        return result;
    }

    const FrameContext& acquired = frame.acquire.frame;
    if (!acquired.depth.Valid()) {
        result.failure = D3D12CarrierExecutionFailure::MissingDepth;
        return result;
    }
    if (!acquired.motionVectors.Valid()) {
        result.failure = D3D12CarrierExecutionFailure::MissingMotion;
        return result;
    }
    if (!ValidMotionScale(config.motionScaleX) ||
        !ValidMotionScale(config.motionScaleY)) {
        result.failure = D3D12CarrierExecutionFailure::InvalidMotionScale;
        return result;
    }

    bool beforeUpscale = false;
    switch (frame.session.decision.pipeline.placement) {
    case NrPlacement::PreSr:
        beforeUpscale = true;
        break;
    case NrPlacement::PostSr:
        beforeUpscale = false;
        break;
    case NrPlacement::Auto:
    case NrPlacement::DeferredResidual:
    case NrPlacement::AcrossRr:
        result.failure = D3D12CarrierExecutionFailure::UnsupportedPlacement;
        return result;
    }

    auto& plan = result.plan;
    plan.framePlan.colorSurface = beforeUpscale
        ? acquired.renderResolution : acquired.outputResolution;
    plan.framePlan.depthSurface = acquired.depth.resolution;
    plan.framePlan.motionSurface = acquired.motionVectors.resolution;
    plan.framePlan.activeColor = FullRect(plan.framePlan.colorSurface);
    plan.framePlan.depth = FullRect(plan.framePlan.depthSurface);
    plan.framePlan.motion = FullRect(plan.framePlan.motionSurface);
    plan.framePlan.execution.workingScale =
        frame.session.decision.workingScale;
    plan.framePlan.execution.passes = config.passes;
    plan.framePlan.execution.unlockPasses = config.unlockPasses;
    plan.framePlan.execution.proxyBackend = config.proxyBackend;
    plan.framePlan.beforeUpscale = beforeUpscale;

    if (!BuildD3D12NrFramePlan(plan.framePlan, plan.resolvedPlan)) {
        result.failure = D3D12CarrierExecutionFailure::InvalidPlan;
        return result;
    }

    plan.submissionEpoch = work.submissionEpoch;
    plan.reset = acquired.resetHistory || acquired.cameraCut;
    plan.depthInverted = config.depthInverted;
    plan.motionScaleX = config.motionScaleX;
    plan.motionScaleY = config.motionScaleY;
    return result;
}

} // namespace nrfusion
