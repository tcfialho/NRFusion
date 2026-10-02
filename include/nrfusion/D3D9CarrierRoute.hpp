#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D9CarrierVariant : std::uint8_t {
    Classic,
    Ex
};

enum class D3D9CarrierRouteFailure : std::uint8_t {
    None,
    ClassicNoGpuSharedSurface,
    MissingD3D9Ex,
    MissingWddm,
    AdapterMismatch,
    InvalidColor,
    UnsupportedFormat,
    InvalidSharedTexture,
    MissingD3D11Open,
    MissingManualSyncQueue,
    BlockingSyncPolicy,
    InsufficientQueueDepth,
    MissingD3D11NtShare,
    MissingD3D12NtImport,
    MissingGpuCopy,
    MissingComposeBack,
    ResetOwnershipUndefined
};

struct D3D9CarrierRouteFacts {
    D3D9CarrierVariant variant = D3D9CarrierVariant::Classic;
    bool d3d9ExAvailable = false;
    bool wddm = false;
    bool sameAdapter = false;
    bool colorTexture2D = false;
    ResourceFormat colorFormat = ResourceFormat::Unknown;
    bool defaultPool = false;
    bool singleMip = false;
    bool noMsaa = false;
    bool sharedHandle = false;
    bool d3d11OpenSharedResource = false;
    bool manualSurfaceQueue = false;
    bool dequeueTimeoutZero = false;
    bool enqueueDoNotWait = false;
    bool flushDoNotWait = false;
    std::uint32_t queueDepth = 0;
    bool d3d11NtSharedSurface = false;
    bool d3d12OpenNtHandle = false;
    bool gpuCopyInbound = false;
    bool gpuCopyOutbound = false;
    bool composeBackGpu = false;
    bool resetOwnershipDefined = false;
};

struct D3D9CarrierRoutePlan {
    std::uint8_t inboundFullFrameCopies = 0;
    std::uint8_t outboundFullFrameCopies = 0;
    bool usesManualSurfaceQueue = false;
    bool nonBlockingBackpressure = false;
    bool colorOnly = true;
};

struct D3D9CarrierRouteResult {
    D3D9CarrierRoutePlan plan{};
    D3D9CarrierRouteFailure failure =
        D3D9CarrierRouteFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D9CarrierRouteFailure::None;
    }
};

D3D9CarrierRouteResult QualifyD3D9CarrierRoute(
    const D3D9CarrierRouteFacts& facts) noexcept;

} // namespace nrfusion
