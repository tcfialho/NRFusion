#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D12GuideRole : std::uint8_t {
    Depth,
    Motion
};

enum class D3D12TypelessGuideFamily : std::uint8_t {
    R32,
    R16,
    R24G8,
    R32G8X24,
    R32G32,
    R16G16,
    R8G8B8A8,
    R16G16B16A16
};

ResourceFormat NormalizeD3D12TypelessGuideFormat(
    D3D12GuideRole role,
    D3D12TypelessGuideFamily family) noexcept;

} // namespace nrfusion
