#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>

#include "nrfusion/FrameContractProvider.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D9ExNativeAcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    MissingDevice,
    MissingColor,
    DeviceMismatch,
    InvalidSurface,
    UnsupportedFormat,
    UnsupportedPool
};

struct D3D9ExNativeAcquireInput {
    ProviderInput identity{};
    IDirect3DDevice9Ex* device = nullptr;
    IDirect3DSurface9* color = nullptr;
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

struct D3D9ExNativeAcquireResult {
    FrameContext frame{};
    D3D9ExNativeAcquireFailure failure =
        D3D9ExNativeAcquireFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure ==
               D3D9ExNativeAcquireFailure::None;
    }
};

D3D9ExNativeAcquireResult AcquireD3D9ExNativeFrame(
    const D3D9ExNativeAcquireInput& input) noexcept;

} // namespace nrfusion
