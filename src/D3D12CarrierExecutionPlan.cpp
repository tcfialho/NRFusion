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

    if (!frame || !frame.session.decision.supported ||
        !frame.session.decision.pipeline.supported ||
        frame.acquire.frame.api != GraphicsApi::D3D12 ||
        frame.session.decision.pipeline.api != GraphicsApi::D3D12 ||
        frame.acquire.frame.frameId != frame.session.frameId ||
        frame.acquire.frame.configurationGeneration !=
            frame.session.configurationGeneration) {
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
    bool beforeUpscale = false;
    bool modelStage = true;
    bool runBeforeUpscale = false;
    bool rayReconstruction = false;
    bool residualAcrossRr = false;

    switch (frame.session.decision.pipeline.placement) {
    case NrPlacement::PreSr:
        if (config.stage != D3D12CarrierExecutionStage::Direct) {
            result.failure = D3D12CarrierExecutionFailure::StageMismatch;
            return result;
        }
        beforeUpscale = true;
        runBeforeUpscale = true;
        break;
    case NrPlacement::PostSr:
        if (config.stage != D3D12CarrierExecutionStage::Direct) {
            result.failure = D3D12CarrierExecutionFailure::StageMismatch;
            return result;
        }
        break;
    case NrPlacement::AcrossRr:
        if (config.stage == D3D12CarrierExecutionStage::AcrossRrStore) {
            beforeUpscale = true;
            runBeforeUpscale = true;
            rayReconstruction = true;
            residualAcrossRr = true;
        } else if (config.stage == D3D12CarrierExecutionStage::AcrossRrApply) {
            modelStage = false;
            runBeforeUpscale = true;
            rayReconstruction = true;
            residualAcrossRr = true;
        } else {
            result.failure = D3D12CarrierExecutionFailure::StageMismatch;
            return result;
        }
        break;
    case NrPlacement::Auto:
    case NrPlacement::DeferredResidual:
        result.failure = D3D12CarrierExecutionFailure::UnsupportedPlacement;
        return result;
    }

    if (modelStage) {
        if (!acquired.depth.Valid()) {
            result.failure = D3D12CarrierExecutionFailure::MissingDepth;
            return result;
        }
        if (!acquired.DepthReliable()) {
            result.failure = D3D12CarrierExecutionFailure::UnreliableDepth;
            return result;
        }
        if (!acquired.motionVectors.Valid()) {
            result.failure = D3D12CarrierExecutionFailure::MissingMotion;
            return result;
        }
        const MotionSource motion = frame.session.decision.pipeline.motion;
        if (motion != MotionSource::Native &&
            motion != MotionSource::DlssContract) {
            result.failure =
                D3D12CarrierExecutionFailure::UnsupportedMotionSource;
            return result;
        }
        if (!acquired.MotionReliable(motion)) {
            result.failure =
                D3D12CarrierExecutionFailure::MotionSourceMismatch;
            return result;
        }
        if (config.useGameExposure && !acquired.exposure.Valid()) {
            result.failure = D3D12CarrierExecutionFailure::MissingExposure;
            return result;
        }
        if (!ValidMotionScale(config.motionScaleX) ||
            !ValidMotionScale(config.motionScaleY)) {
            result.failure = D3D12CarrierExecutionFailure::InvalidMotionScale;
            return result;
        }
    }

    auto& plan = result.plan;
    plan.framePlan.beforeUpscale = beforeUpscale;
    plan.submissionEpoch = work.submissionEpoch;
    plan.reset = acquired.resetHistory || acquired.cameraCut;
    plan.depthInverted = config.depthInverted;
    plan.colourIsLinearHdr = acquired.hdr;
    plan.modelStage = modelStage;
    plan.requiresColor = modelStage && beforeUpscale;
    plan.runBeforeUpscale = runBeforeUpscale;
    plan.rayReconstruction = rayReconstruction;
    plan.residualAcrossRr = residualAcrossRr;
    plan.motionScaleX = config.motionScaleX;
    plan.motionScaleY = config.motionScaleY;

    if (!modelStage) return result;

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

    if (!BuildD3D12NrFramePlan(plan.framePlan, plan.resolvedPlan)) {
        result.failure = D3D12CarrierExecutionFailure::InvalidPlan;
        return result;
    }
    return result;
}

} // namespace nrfusion
