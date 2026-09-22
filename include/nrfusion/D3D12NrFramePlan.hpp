#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

struct D3D12NrSubrect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    constexpr bool Valid() const noexcept { return width != 0 && height != 0; }
};

struct D3D12NrFramePlanConfig {
    float workingScale = 1.0f;
    std::uint32_t passes = 1;
    bool unlockPasses = false;
    bool proxyBackend = false;
};

struct D3D12NrFramePlanInput {
    Resolution colorSurface{};
    Resolution depthSurface{};
    Resolution motionSurface{};
    D3D12NrSubrect activeColor{};
    D3D12NrSubrect depth{};
    D3D12NrSubrect motion{};
    D3D12NrFramePlanConfig execution{};
    bool beforeUpscale = false;
};

struct D3D12NrFramePlan {
    Resolution work{};
    D3D12NrSubrect activeColor{};
    D3D12NrSubrect depth{};
    D3D12NrSubrect motion{};
    float workingScale = 1.0f;
    float motionToWorkX = 1.0f;
    float motionToWorkY = 1.0f;
    std::uint32_t requestedPasses = 1;
    bool reduced = false;
    bool cropColor = false;
};

bool BuildD3D12NrFramePlan(const D3D12NrFramePlanInput& input,
                           D3D12NrFramePlan& output) noexcept;

} // namespace nrfusion
