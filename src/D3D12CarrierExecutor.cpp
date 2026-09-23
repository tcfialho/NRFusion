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

bool BelongsToDevice(
    ID3D12Device* expected, ID3D12DeviceChild* child) noexcept {
    if (expected == nullptr || child == nullptr) return false;
    ID3D12Device* actual = nullptr;
    const HRESULT hr = child->GetDevice(IID_PPV_ARGS(&actual));
    const bool matches = SUCCEEDED(hr) && actual == expected;
    if (actual != nullptr) actual->Release();
    return matches;
}

bool SameFrameIdentity(
    const D3D12NativeFrameResources& resources,
    const D3D12CarrierFrameResult& frame) noexcept {
    return resources.identity.frameId == frame.acquire.frame.frameId &&
           resources.identity.configurationGeneration ==
               frame.acquire.frame.configurationGeneration;
}

} // namespace

bool D3D12CarrierExecutor::BindDeviceAfterIdle(ID3D12Device* device) {
    if (device == nullptr) return false;
    if (boundDevice_ == device) return executor_.Init(device);

    if (boundDevice_ != nullptr) {
        executor_.Shutdown();
        boundDevice_->Release();
        boundDevice_ = nullptr;
    }

    if (!executor_.Load() || !executor_.Init(device)) return false;
    device->AddRef();
    boundDevice_ = device;
    return true;
}

void D3D12CarrierExecutor::ShutdownAfterIdle() {
    executor_.Shutdown();
    if (boundDevice_ != nullptr) boundDevice_->Release();
    boundDevice_ = nullptr;
}

D3D12CarrierExecuteResult D3D12CarrierExecutor::Execute(
    ID3D12GraphicsCommandList* cmdList,
    D3D12CarrierSession& session,
    const D3D12NativeFrameResources& resources,
    const D3D12CarrierFrameResult& frame,
    const D3D12CarrierWork& work,
    const D3D12CarrierExecuteOptions& options) {
    D3D12CarrierExecuteResult result{};

    D3D12CarrierExecutionConfig execution = options.execution;
    execution.useGameExposure = options.composition.useGameExposure;
    const auto planned = BuildD3D12CarrierExecutionPlan(
        frame, work, execution);
    if (!planned) {
        result.failure = D3D12CarrierExecuteFailure::Planning;
        result.planningFailure = planned.failure;
        return result;
    }
    if (boundDevice_ == nullptr) {
        result.failure = D3D12CarrierExecuteFailure::NotInitialized;
        return result;
    }
    if (!session.CanExecuteWork(work)) {
        result.failure = D3D12CarrierExecuteFailure::InactiveWork;
        return result;
    }
    if (cmdList == nullptr) {
        result.failure = D3D12CarrierExecuteFailure::InvalidCommandList;
        return result;
    }
    if (!BelongsToDevice(boundDevice_, cmdList) ||
        !BelongsToDevice(boundDevice_, resources.output) ||
        (resources.color.resource != nullptr &&
         !BelongsToDevice(boundDevice_, resources.color.resource)) ||
        (resources.depth.resource != nullptr &&
         !BelongsToDevice(boundDevice_, resources.depth.resource)) ||
        (resources.motionVectors.resource != nullptr &&
         !BelongsToDevice(boundDevice_, resources.motionVectors.resource)) ||
        (resources.exposure.resource != nullptr &&
         !BelongsToDevice(boundDevice_, resources.exposure.resource))) {
        result.failure = D3D12CarrierExecuteFailure::DeviceMismatch;
        return result;
    }

    const FrameContext& normalized = frame.acquire.frame;
    if (!SameFrameIdentity(resources, frame) ||
        resources.output == nullptr ||
        frame.acquire.outputOpaqueId == 0 ||
        OpaqueId(resources.output) != frame.acquire.outputOpaqueId) {
        result.failure = D3D12CarrierExecuteFailure::ResourceIdentityMismatch;
        return result;
    }
    if (planned.plan.modelStage &&
        ((!Matches(resources.depth, normalized.depth)) ||
         (!Matches(resources.motionVectors, normalized.motionVectors)) ||
         (planned.plan.requiresColor &&
          !Matches(resources.color, normalized.color)))) {
        result.failure = D3D12CarrierExecuteFailure::ResourceIdentityMismatch;
        return result;
    }
    if (planned.plan.modelStage && options.composition.useGameExposure &&
        (resources.exposure.resource == nullptr ||
         !Matches(resources.exposure, normalized.exposure))) {
        result.failure = D3D12CarrierExecuteFailure::MissingExposure;
        return result;
    }

    if (!session.ClaimExecuteWork(work)) {
        result.failure = D3D12CarrierExecuteFailure::InactiveWork;
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
    request.composition.runBeforeUpscale = planned.plan.runBeforeUpscale;
    request.composition.rayReconstruction = planned.plan.rayReconstruction;
    request.composition.residualAcrossRr = planned.plan.residualAcrossRr;
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
