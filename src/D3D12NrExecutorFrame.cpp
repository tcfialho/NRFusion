#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

D3D12NrFrameResult D3D12NrExecutor::ExecuteFrame(
    ID3D12GraphicsCommandList* cmdList, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request) {
    if (cmdList == nullptr || resources.color == nullptr || resources.depth == nullptr ||
        resources.motion == nullptr || resources.output == nullptr)
        return D3D12NrFrameResult::Failed;

    const bool before = request.plan.beforeUpscale;
    const bool acrossRr = request.composition.residualAcrossRr &&
                          request.composition.runBeforeUpscale &&
                          request.composition.rayReconstruction;
    if (acrossRr && !before)
        return ApplyStoredResidual(cmdList, resources, request);
    if (request.composition.runBeforeUpscale != before)
        return D3D12NrFrameResult::SkippedPlacement;
    return ExecuteMainFrame(cmdList, resources, request);
}

D3D12NrFrameResult D3D12NrExecutor::ExecuteMainFrame(
    ID3D12GraphicsCommandList* cmdList, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request) {
    FrameContext context{};
    if (!BuildD3D12NrFramePlan(request.plan, context.plan))
        return D3D12NrFrameResult::Failed;

    context.target = request.plan.beforeUpscale ? resources.color : resources.output;
    context.targetState = request.plan.beforeUpscale ? request.colorState : request.outputState;
    context.targetArrival = context.targetState;
    context.depthState = request.depthState;
    context.motionState = request.motionState;
    context.exposureState = request.exposureState;
    context.acrossRr = request.composition.residualAcrossRr &&
                       request.composition.runBeforeUpscale &&
                       request.composition.rayReconstruction;

    ID3D12Device* device = nullptr;
    if (FAILED(context.target->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
        return D3D12NrFrameResult::Failed;
    const D3D12_RESOURCE_DESC targetDesc = context.target->GetDesc();
    context.targetFormat = targetDesc.Format;
    context.cropColor = context.plan.cropColor;
    context.targetSupportsUav = context.cropColor ||
        (targetDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    D3D12NrScratchDesc scratchDesc{
        targetDesc.Format, context.plan.activeColor.width, context.plan.activeColor.height,
        context.plan.work.width, context.plan.work.height};
    if (request.reset || !scratch_.Matches(scratchDesc)) {
        residualHistoryPrimed_ = false;
        residualStoreValid_ = false;
    }
    bool ok = codec_.Init(device) && scratch_.Ensure(device, scratchDesc, retirement_);
    if (context.plan.requestedPasses > 1)
        ok = ok && scratch_.EnsureOptional(
            device, D3D12NrScratchKind::PassScratch, targetDesc.Format,
            context.plan.work.width, context.plan.work.height, retirement_);
    if (context.plan.reduced)
        ok = ok && scratch_.EnsureOptional(
            device, D3D12NrScratchKind::ColorSmall, targetDesc.Format,
            context.plan.work.width, context.plan.work.height, retirement_);
    if (context.cropColor)
        ok = ok && scratch_.EnsureOptional(
            device, D3D12NrScratchKind::ActiveColor, targetDesc.Format,
            context.plan.activeColor.width, context.plan.activeColor.height, retirement_);
    if (context.acrossRr) {
        const D3D12_RESOURCE_DESC outputDesc = resources.output->GetDesc();
        ok = ok &&
            scratch_.EnsureOptional(
                device, D3D12NrScratchKind::ResidualEdited, targetDesc.Format,
                context.plan.activeColor.width, context.plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(
                device, D3D12NrScratchKind::ResidualHistory0,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                context.plan.activeColor.width, context.plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(
                device, D3D12NrScratchKind::ResidualHistory1,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                context.plan.activeColor.width, context.plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(
                device, D3D12NrScratchKind::ResidualComposed, outputDesc.Format,
                static_cast<std::uint32_t>(outputDesc.Width), outputDesc.Height, retirement_);
    }
    device->Release();
    if (!ok) return D3D12NrFrameResult::Failed;

    if (!EnsureFeatureForEpoch(
            cmdList, context.plan.work.width, context.plan.work.height,
            request.submissionEpoch, request.tuning[0]))
        return D3D12NrFrameResult::Failed;
    if (!submissionGate_.ReadyFor(request.submissionEpoch))
        return D3D12NrFrameResult::PendingFeature;

    bool pending = false;
    const std::uint32_t effectivePasses = PreparePassFeatures(
        cmdList, context.plan.work.width, context.plan.work.height,
        context.plan.requestedPasses, request.submissionEpoch, request.tuning, pending);
    if (pending) return D3D12NrFrameResult::PendingFeature;
    if (effectivePasses == 0) return D3D12NrFrameResult::Failed;

    if (!PrepareFrameResources(cmdList, resources, request, context)) {
        RestoreFrameResources(cmdList, resources, request, context);
        return D3D12NrFrameResult::Failed;
    }

    const bool modelOk = RunFrameModel(
        cmdList, resources, request, context, effectivePasses);
    RestoreFrameResources(cmdList, resources, request, context);
    if (!modelOk) {
        residualStoreValid_ = false;
        return D3D12NrFrameResult::Failed;
    }
    return D3D12NrFrameResult::Applied;
}

} // namespace nrfusion
