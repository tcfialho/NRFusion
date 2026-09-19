#include "nrfusion/MotionNormalization.hpp"

#include <cmath>
#include <limits>

namespace nrfusion {
namespace {

bool ValidConfig(const MotionNormalizationConfig& config) noexcept {
    if (!config.targetVectorDomain.Valid() || !std::isfinite(config.componentScaleX) ||
        !std::isfinite(config.componentScaleY))
        return false;
    switch (config.units) {
    case MotionUnits::Pixels:
        return config.sourceVectorDomain.Valid();
    case MotionUnits::NormalizedUv:
        return true;
    }
    return false;
}

double AxisScale(std::uint32_t source, std::uint32_t target, MotionUnits units,
                 double componentScale) noexcept {
    if (units == MotionUnits::NormalizedUv)
        return static_cast<double>(target) * componentScale;
    return (static_cast<double>(target) / static_cast<double>(source)) * componentScale;
}

} // namespace

std::optional<std::pair<float, float>> MotionNormalizer::NormalizeVector(
    float x, float y, const MotionNormalizationConfig& config) noexcept {
    if (!ValidConfig(config) || !std::isfinite(x) || !std::isfinite(y)) return std::nullopt;

    const double sx = AxisScale(config.sourceVectorDomain.width, config.targetVectorDomain.width,
                                config.units, config.componentScaleX);
    const double sy = AxisScale(config.sourceVectorDomain.height, config.targetVectorDomain.height,
                                config.units, config.componentScaleY);
    const double normalizedX = static_cast<double>(x) * sx;
    const double normalizedY = static_cast<double>(y) * sy;
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        std::fabs(normalizedX) > static_cast<double>(std::numeric_limits<float>::max()) ||
        std::fabs(normalizedY) > static_cast<double>(std::numeric_limits<float>::max()))
        return std::nullopt;
    return std::pair<float, float>{static_cast<float>(normalizedX), static_cast<float>(normalizedY)};
}

NormalizedMotionField MotionNormalizer::NormalizeField(const std::vector<float>& x,
                                                        const std::vector<float>& y,
                                                        const MotionNormalizationConfig& config) {
    NormalizedMotionField out;
    if (!ValidConfig(config) || x.empty() || x.size() != y.size()) return out;
    out.validShape = true;
    out.x.resize(x.size(), 0.0f);
    out.y.resize(y.size(), 0.0f);
    out.valid.resize(x.size(), 0u);
    std::size_t validCount = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const auto vector = NormalizeVector(x[i], y[i], config);
        if (!vector) continue;
        out.x[i] = vector->first;
        out.y[i] = vector->second;
        out.valid[i] = 1u;
        ++validCount;
    }
    out.validPixelRatio = x.empty() ? 0.0
                                    : static_cast<double>(validCount) / static_cast<double>(x.size());
    return out;
}

} // namespace nrfusion
