#include "nrfusion/D3D12CarrierExecutor.hpp"

namespace nrfusion {
namespace {

bool Matches(
    const D3D12NativeResourceInput& native,
    const ResourceRef& normalized) noexcept {
    if (!normalized.Valid()) return native.resource == nullptr;
    if (native.resource == nullptr) return false;
    return normalized.opaqueId == static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(native.resource));
}

std::uint64_t OpaqueId(ID3D12Resource* resource) noexcept {
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(resource));
}

bool SameFrameIdentity(
    const D3D12NativeFrameResources& resources,
    const D3D12CarrierFrameResult& frame) noexcept {
    return resources.identity.frameId == frame.acquire.frame.frameId &&
           resources.identity.configurationGeneration ==
               frame.acquire.frame.configurationGeneration;
}

} // namespace

D3D12CarrierExecuteResult D3D12CarrierExecutor::Execute(
    ID3D12GraphicsCommandList* cmdList,
    const D3D12NativeFrameResources& resources,
    const D3D12CarrierFrameResult& frame,
    const D3D12CarrierWork& work,
    const D3D12CarrierExecuteOptions& options) {
    D3D12CarrierExecuteResult result{};

    const auto planned = BuildD3D12CarrierExecutionPlan(
        frame, work, options.execution);
    if (!planned) {
        result.failure = D3D12CarrierExecuteFailure::Planning;
        result.planningFailure = planned.failure;
        return result;
    }
    if (cmdList == nullptr) {
        result.failure = D3D12CarrierExecuteFailure::InvalidCommandList;
        return result;
    }

    const FrameContext& normalized = frame.acquire.frame;
    if (!SameFrameIdentity(resources, frame) ||
        resources.output == nullptr ||
        frame.acquire.outputOpaqueId == 0 ||
        OpaqueId(resources.output) != frame.acquire.outputOpaqueId ||
        !Matches(resources.color, normalized.color) ||
        !Matches(resources.depth, normalized.depth) ||
        !Matches(resources.motionVectors, normalized.motionVectors)) {
        result.failure = D3D12CarrierExecuteFailure::ResourceIdentityMismatch;
        return result;
    }
    if (options.composition.useGameExposure &&
        !Matches(resources.exposure, normalized.exposure)) {
        result.failure = D3D12CarrierExecuteFailure::MissingExposure;
        return result;
    }

    D3D12NrFrameResources executorResources{};
    executorResources.color = resources.color.resource;
    executorResources.depth = resources.depth.resource;
    executorResources.motion = resources.motionVectors.resource;
    executorResources.output = resources.output;
    executorResources.exposure = resources.exposure.resource;

    D3D12NrFrameRequest request{};
    request.plan = planned.plan.framePlan;
    request.tuning = options.tuning;
    request.composition = options.composition;
    request.composition.runBeforeUpscale =
        planned.plan.framePlan.beforeUpscale;
    request.composition.rayReconstruction = false;
    request.composition.residualAcrossRr = false;
    request.composition.colourIsLinearHdr =
        planned.plan.colourIsLinearHdr;
    request.submissionEpoch = planned.plan.submissionEpoch;
    request.reset = planned.plan.reset;
    request.depthInverted = planned.plan.depthInverted;
    request.motionScaleX = planned.plan.motionScaleX;
    request.motionScaleY = planned.plan.motionScaleY;
    request.colorState = options.colorState;
    request.outputState = options.outputState;
    request.depthState = options.depthState;
    request.motionState = options.motionState;
    request.exposureState = options.exposureState;

    result.attempted = true;
    result.executorResult =
        executor_.ExecuteFrame(cmdList, executorResources, request);
    if (result.executorResult == D3D12NrFrameResult::Failed)
        result.failure = D3D12CarrierExecuteFailure::ExecutorFailed;
    return result;
}

} // namespace nrfusion
