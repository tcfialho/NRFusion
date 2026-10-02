#include "nrfusion/D3D9CarrierRoute.hpp"

namespace nrfusion {

D3D9CarrierRouteResult QualifyD3D9CarrierRoute(
    const D3D9CarrierRouteFacts& facts) noexcept {
    D3D9CarrierRouteResult result{};

    if (facts.variant == D3D9CarrierVariant::Classic) {
        result.failure =
            D3D9CarrierRouteFailure::ClassicNoGpuSharedSurface;
        return result;
    }
    if (!facts.d3d9ExAvailable) {
        result.failure = D3D9CarrierRouteFailure::MissingD3D9Ex;
        return result;
    }
    if (!facts.wddm) {
        result.failure = D3D9CarrierRouteFailure::MissingWddm;
        return result;
    }
    if (!facts.sameAdapter) {
        result.failure = D3D9CarrierRouteFailure::AdapterMismatch;
        return result;
    }
    if (!facts.colorTexture2D) {
        result.failure = D3D9CarrierRouteFailure::InvalidColor;
        return result;
    }
    if (facts.colorFormat != ResourceFormat::Rgba16Float) {
        result.failure = D3D9CarrierRouteFailure::UnsupportedFormat;
        return result;
    }
    if (!facts.defaultPool || !facts.singleMip ||
        !facts.noMsaa || !facts.sharedHandle) {
        result.failure =
            D3D9CarrierRouteFailure::InvalidSharedTexture;
        return result;
    }
    if (!facts.d3d11OpenSharedResource) {
        result.failure = D3D9CarrierRouteFailure::MissingD3D11Open;
        return result;
    }
    if (!facts.manualSurfaceQueue) {
        result.failure =
            D3D9CarrierRouteFailure::MissingManualSyncQueue;
        return result;
    }
    if (!facts.dequeueTimeoutZero ||
        !facts.enqueueDoNotWait ||
        !facts.flushDoNotWait) {
        result.failure =
            D3D9CarrierRouteFailure::BlockingSyncPolicy;
        return result;
    }
    if (facts.queueDepth < 2) {
        result.failure =
            D3D9CarrierRouteFailure::InsufficientQueueDepth;
        return result;
    }
    if (!facts.d3d11NtSharedSurface) {
        result.failure =
            D3D9CarrierRouteFailure::MissingD3D11NtShare;
        return result;
    }
    if (!facts.d3d12OpenNtHandle) {
        result.failure =
            D3D9CarrierRouteFailure::MissingD3D12NtImport;
        return result;
    }
    if (!facts.gpuCopyInbound || !facts.gpuCopyOutbound) {
        result.failure = D3D9CarrierRouteFailure::MissingGpuCopy;
        return result;
    }
    if (!facts.composeBackGpu) {
        result.failure =
            D3D9CarrierRouteFailure::MissingComposeBack;
        return result;
    }
    if (!facts.resetOwnershipDefined) {
        result.failure =
            D3D9CarrierRouteFailure::ResetOwnershipUndefined;
        return result;
    }

    result.plan.inboundFullFrameCopies = 2;
    result.plan.outboundFullFrameCopies = 2;
    result.plan.usesManualSurfaceQueue = true;
    result.plan.nonBlockingBackpressure = true;
    result.plan.colorOnly = true;
    return result;
}

} // namespace nrfusion
