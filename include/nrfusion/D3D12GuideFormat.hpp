#pragma once

#include "nrfusion/FrameContract.hpp"

#include <dxgiformat.h>
#include <cstdint>

namespace nrfusion {

enum class D3D12GuideRole : std::uint8_t {
    Depth,
    Motion
};

enum class D3D12TypelessGuideFamily : std::uint8_t {
    Unknown = 0,
    R32,
    R16,
    R24G8,
    R32G8X24,
    R32G32,
    R16G16,
    R8G8B8A8,
    R16G16B16A16
};

ResourceFormat NormalizeD3D12TypedGuideFormat(
    D3D12GuideRole role, ResourceFormat format) noexcept;

ResourceFormat NormalizeD3D12TypelessGuideFormat(
    D3D12GuideRole role,
    D3D12TypelessGuideFamily family) noexcept;

bool CanUseDirectD3D12Guide(bool runtimeQualified, D3D12GuideRole role,
                           D3D12TypelessGuideFamily family, Resolution surface) noexcept;

D3D12TypelessGuideFamily ClassifyD3D12GuideFormat(DXGI_FORMAT format) noexcept;
bool IsDirectD3D12GuideCandidate(D3D12GuideRole role, DXGI_FORMAT format) noexcept;

} // namespace nrfusion
