#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D10BridgeRouteFailure : std::uint8_t {
    None,
    MissingD3D10_1,
    AdapterMismatch,
    InvalidSource,
    UnsupportedFormat,
    MissingLegacySharedSurface,
    MissingKeyedMutex,
    BlockingMutexPolicy,
    MissingD3D11LegacyOpen,
    MissingD3D11NtShare,
    MissingD3D12NtImport,
    MissingGpuCopy,
    MissingComposeBack
};

struct D3D10BridgeRouteFacts {
    bool d3d10_1 = false;
    bool sameAdapter = false;
    bool sourceTexture2D = false;
    ResourceFormat sourceFormat = ResourceFormat::Unknown;
    bool legacySharedSurface = false;
    bool keyedMutex = false;
    bool zeroTimeoutAcquire = false;
    bool d3d11OpenLegacy = false;
    bool d3d11NtSharedSurface = false;
    bool d3d12OpenNtHandle = false;
    bool gpuCopyInbound = false;
    bool gpuCopyOutbound = false;
    bool composeBackGpu = false;
};

struct D3D10BridgeRoutePlan {
    std::uint8_t inboundFullFrameCopies = 0;
    std::uint8_t outboundFullFrameCopies = 0;
    bool usesLegacyDxgiHandle = false;
    bool usesNtHandleAfterD3D11 = false;
    bool usesZeroTimeoutKeyedMutex = false;
};

struct D3D10BridgeRouteResult {
    D3D10BridgeRoutePlan plan{};
    D3D10BridgeRouteFailure failure =
        D3D10BridgeRouteFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D10BridgeRouteFailure::None;
    }
};

D3D10BridgeRouteResult QualifyD3D10BridgeRoute(
    const D3D10BridgeRouteFacts& facts) noexcept;

} // namespace nrfusion
