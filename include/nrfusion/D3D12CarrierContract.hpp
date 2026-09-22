#pragma once

#include "nrfusion/FrameContractProvider.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D12AcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    InvalidDimensions,
    InvalidJitter,
    MissingColor,
    UnprovenResource,
    InvalidResource,
    InvalidEvidence,
    StaleResource,
    ColorResolutionMismatch
};

struct D3D12AcquiredResource {
    ResourceRef resource{};
    bool acquired = false;
};

struct D3D12AcquireSnapshot {
    ProviderInput identity{};
    Resolution renderResolution{};
    Resolution outputResolution{};
    Jitter jitter{};

    D3D12AcquiredResource color{};
    D3D12AcquiredResource depth{};
    D3D12AcquiredResource motionVectors{};
    D3D12AcquiredResource exposure{};
    D3D12AcquiredResource reactiveMask{};
    std::uint64_t outputOpaqueId = 0;

    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

struct D3D12AcquireResult {
    FrameContext frame{};
    ProviderDiagnostics diagnostics{};
    std::uint64_t outputOpaqueId = 0;
    D3D12AcquireFailure failure = D3D12AcquireFailure::None;
    bool attempted = false;

    constexpr explicit operator bool() const noexcept {
        return attempted && failure == D3D12AcquireFailure::None;
    }
};

D3D12AcquireResult BuildD3D12FrameContract(
    const D3D12AcquireSnapshot& snapshot) noexcept;

} // namespace nrfusion
