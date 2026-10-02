#pragma once

#include "nrfusion/D3D12CarrierContract.hpp"

#include <cstdint>

namespace nrfusion {

struct D3D12NativeTextureFacts {
    std::uint64_t opaqueId = 0;
    Resolution resolution{};
    ResourceFormat format = ResourceFormat::Unknown;
    std::uint16_t depthOrArraySize = 0;
    std::uint16_t mipLevels = 0;
    std::uint32_t sampleCount = 0;
    bool texture2D = false;
};

struct D3D12NativeResourceCapture {
    D3D12NativeTextureFacts texture{};
    ResourceProvenance provenance = ResourceProvenance::Unknown;
    ResourceReliability reliability = ResourceReliability::Unknown;
};

struct D3D12NativeAcquireInput {
    ProviderInput identity{};
    D3D12NativeResourceCapture color{};
    D3D12NativeResourceCapture depth{};
    D3D12NativeResourceCapture motionVectors{};
    D3D12NativeResourceCapture exposure{};
    D3D12NativeResourceCapture reactiveMask{};
    D3D12NativeTextureFacts output{};
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

enum class D3D12NativeAcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    MissingColor,
    MissingOutput,
    InvalidTexture,
    InvalidEvidence
};

struct D3D12NativeAcquireResult {
    D3D12AcquireSnapshot snapshot{};
    D3D12NativeAcquireFailure failure = D3D12NativeAcquireFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D12NativeAcquireFailure::None;
    }
};

D3D12NativeAcquireResult BuildD3D12NativeAcquireSnapshot(
    const D3D12NativeAcquireInput& input) noexcept;

} // namespace nrfusion
