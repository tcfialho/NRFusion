#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>

#include "nrfusion/FrameContractProvider.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D11NativeAcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    MissingContext,
    MissingColor,
    DeviceMismatch,
    InvalidTexture,
    UnsupportedFormat
};

struct D3D11NativeAcquireInput {
    ProviderInput identity{};
    ID3D11DeviceContext* context = nullptr;
    ID3D11Resource* color = nullptr;
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

struct D3D11NativeAcquireResult {
    FrameContext frame{};
    D3D11NativeAcquireFailure failure = D3D11NativeAcquireFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D11NativeAcquireFailure::None;
    }
};

D3D11NativeAcquireResult AcquireD3D11NativeFrame(
    const D3D11NativeAcquireInput& input) noexcept;

} // namespace nrfusion
