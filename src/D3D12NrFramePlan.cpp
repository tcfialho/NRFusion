#include "nrfusion/D3D12NrFramePlan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {

constexpr std::uint32_t kDefaultMaxPassCount = 3;
constexpr std::uint32_t kMaxPassCount = 30;

bool Fits(const D3D12NrSubrect& rect, Resolution surface) noexcept {
    if (!rect.Valid() || !surface.Valid()) return false;
    if (rect.x > surface.width || rect.y > surface.height) return false;
    return rect.width <= surface.width - rect.x && rect.height <= surface.height - rect.y;
}

bool ScaleDimension(std::uint32_t value, float scale, std::uint32_t& out) noexcept {
    const double scaled = static_cast<double>(value) * static_cast<double>(scale) + 0.5;
    if (!std::isfinite(scaled) || scaled < 1.0 ||
        scaled > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return false;
    out = static_cast<std::uint32_t>(scaled);
    return true;
}

} // namespace

bool BuildD3D12NrFramePlan(const D3D12NrFramePlanInput& input,
                           D3D12NrFramePlan& output) noexcept {
    output = {};
    if (!Fits(input.activeColor, input.colorSurface) ||
        !Fits(input.depth, input.depthSurface) ||
        !Fits(input.motion, input.motionSurface))
        return false;

    float scale = input.execution.workingScale;
    if (!std::isfinite(scale)) scale = 1.0f;
    scale = std::clamp(scale, 0.25f, 2.0f);

    Resolution work{};
    if (!ScaleDimension(input.activeColor.width, scale, work.width) ||
        !ScaleDimension(input.activeColor.height, scale, work.height))
        return false;

    const std::uint32_t passLimit = input.execution.unlockPasses
        ? kMaxPassCount : kDefaultMaxPassCount;
    const std::uint32_t configured = std::clamp(input.execution.passes, 1u, passLimit);

    output.work = work;
    output.workingScale = scale;
    output.requestedPasses = input.execution.proxyBackend ? 1u : configured;
    output.reduced = work.width != input.activeColor.width || work.height != input.activeColor.height;
    output.cropColor = input.beforeUpscale &&
        (input.activeColor.x != 0 || input.activeColor.y != 0 ||
         input.activeColor.width != input.colorSurface.width ||
         input.activeColor.height != input.colorSurface.height);
    output.motionToWorkX = static_cast<float>(work.width) /
                           static_cast<float>(input.activeColor.width);
    output.motionToWorkY = static_cast<float>(work.height) /
                           static_cast<float>(input.activeColor.height);
    return true;
}

} // namespace nrfusion
