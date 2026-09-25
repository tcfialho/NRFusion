#include "nrfusion/D3D10CarrierRoute.hpp"

namespace nrfusion {

D3D10BridgeRouteResult QualifyD3D10BridgeRoute(
    const D3D10BridgeRouteFacts& facts) noexcept {
    D3D10BridgeRouteResult result{};

    if (!facts.d3d10_1) {
        result.failure =
            D3D10BridgeRouteFailure::MissingD3D10_1;
        return result;
    }
    if (!facts.sameAdapter) {
        result.failure =
            D3D10BridgeRouteFailure::AdapterMismatch;
        return result;
    }
    if (!facts.sourceTexture2D) {
        result.failure =
            D3D10BridgeRouteFailure::InvalidSource;
        return result;
    }
    if (facts.sourceFormat != ResourceFormat::Rgba16Float) {
        result.failure =
            D3D10BridgeRouteFailure::UnsupportedFormat;
        return result;
    }
    if (!facts.legacySharedSurface) {
        result.failure =
            D3D10BridgeRouteFailure::MissingLegacySharedSurface;
        return result;
    }
    if (!facts.keyedMutex) {
        result.failure =
            D3D10BridgeRouteFailure::MissingKeyedMutex;
        return result;
    }
    if (!facts.zeroTimeoutAcquire) {
        result.failure =
            D3D10BridgeRouteFailure::BlockingMutexPolicy;
        return result;
    }
    if (!facts.d3d11OpenLegacy) {
        result.failure =
            D3D10BridgeRouteFailure::MissingD3D11LegacyOpen;
        return result;
    }
    if (!facts.d3d11NtSharedSurface) {
        result.failure =
            D3D10BridgeRouteFailure::MissingD3D11NtShare;
        return result;
    }
    if (!facts.d3d12OpenNtHandle) {
        result.failure =
            D3D10BridgeRouteFailure::MissingD3D12NtImport;
        return result;
    }
    if (!facts.gpuCopyInbound ||
        !facts.gpuCopyOutbound) {
        result.failure =
            D3D10BridgeRouteFailure::MissingGpuCopy;
        return result;
    }
    if (!facts.composeBackGpu) {
        result.failure =
            D3D10BridgeRouteFailure::MissingComposeBack;
        return result;
    }

    result.plan.inboundFullFrameCopies = 2;
    result.plan.outboundFullFrameCopies = 2;
    result.plan.usesLegacyDxgiHandle = true;
    result.plan.usesNtHandleAfterD3D11 = true;
    result.plan.usesZeroTimeoutKeyedMutex = true;
    return result;
}

} // namespace nrfusion
