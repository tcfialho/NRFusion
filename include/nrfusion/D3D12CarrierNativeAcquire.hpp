#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include "nrfusion/D3D12CarrierNativeFacts.hpp"

namespace nrfusion {

struct D3D12NativeResourceInput {
    ID3D12Resource* resource = nullptr;
    ResourceProvenance provenance = ResourceProvenance::Unknown;
    ResourceReliability reliability = ResourceReliability::Unknown;
};

struct D3D12NativeFrameResources {
    ProviderInput identity{};
    D3D12NativeResourceInput color{};
    D3D12NativeResourceInput depth{};
    D3D12NativeResourceInput motionVectors{};
    D3D12NativeResourceInput exposure{};
    D3D12NativeResourceInput reactiveMask{};
    ID3D12Resource* output = nullptr;
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

D3D12NativeAcquireResult AcquireD3D12NativeSnapshot(
    const D3D12NativeFrameResources& resources) noexcept;

} // namespace nrfusion
