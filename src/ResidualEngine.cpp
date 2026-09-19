#include "nrfusion/ResidualEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {

bool PixelCount(std::uint32_t width, std::uint32_t height, std::size_t& count) noexcept {
    if (width == 0 || height == 0) {
        count = 0;
        return false;
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        count = 0;
        return false;
    }
    count = w * h;
    return true;
}

} // namespace

ResidualEngine::ResidualEngine(ResidualEngineConfig config)
    : config_(config), reprojection_(config.reprojection) {
    if (!std::isfinite(config_.minPreExposure) || config_.minPreExposure <= 0.0f)
        config_.minPreExposure = 1.0e-4f;
    if (!std::isfinite(config_.maxPreExposure) || config_.maxPreExposure < config_.minPreExposure)
        config_.maxPreExposure = 1.0e4f;
    if (!std::isfinite(config_.maxExposureRatio) || config_.maxExposureRatio < 1.0f)
        config_.maxExposureRatio = 16.0f;
}

bool ResidualEngine::ValidExposure(float value) const noexcept {
    return std::isfinite(value) && value >= config_.minPreExposure &&
           value <= config_.maxPreExposure;
}

ResidualImage ResidualEngine::Extract(const ResidualExtractInput& in) const {
    ResidualImage out;
    out.width = in.original.width;
    out.height = in.original.height;
    out.sourceFrame = in.frameId;
    if (!ValidExposure(in.preExposure)) return out;
    out.preExposure = in.preExposure;

    std::size_t pixels = 0;
    if (!PixelCount(out.width, out.height, pixels) ||
        in.neural.width != out.width || in.neural.height != out.height ||
        in.original.values.size() != pixels || in.neural.values.size() != pixels)
        return out;

    out.residual.resize(pixels);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float original = in.original.values[i];
        const float neural = in.neural.values[i];
        if (!std::isfinite(original) || !std::isfinite(neural)) {
            out.residual[i] = 0.0f;
            continue;
        }
        const float delta = neural - original;
        out.residual[i] = std::isfinite(delta) ? delta : 0.0f;
    }

    if (in.depth.size() == pixels) {
        out.depth = in.depth;
        for (float& depth : out.depth)
            if (!std::isfinite(depth)) depth = std::numeric_limits<float>::quiet_NaN();
    }
    return out;
}

ResidualImage ResidualEngine::NormalizeExposure(const ResidualImage& history,
                                                float currentPreExposure) const {
    ResidualImage out = history;
    if (!ValidExposure(history.preExposure) || !ValidExposure(currentPreExposure)) {
        out.residual.clear();
        return out;
    }

    const float ratio = currentPreExposure / history.preExposure;
    if (!std::isfinite(ratio) || ratio <= 0.0f || ratio > config_.maxExposureRatio ||
        ratio < 1.0f / config_.maxExposureRatio) {
        out.residual.clear();
        return out;
    }

    for (float& value : out.residual) {
        if (!std::isfinite(value)) {
            value = 0.0f;
            continue;
        }
        const float scaled = value * ratio;
        value = std::isfinite(scaled) ? scaled : 0.0f;
    }
    out.preExposure = currentPreExposure;
    return out;
}

ResidualEngineOutput ResidualEngine::Reproject(const ResidualImage& current,
                                               const ResidualImage& history,
                                               const std::vector<float>& motionX,
                                               const std::vector<float>& motionY,
                                               const std::vector<float>& confidence,
                                               bool cameraCut) const {
    ResidualEngineOutput result;
    std::size_t pixels = 0;
    if (!PixelCount(current.width, current.height, pixels) ||
        current.residual.size() != pixels || !ValidExposure(current.preExposure))
        return result;

    ResidualImage normalizedHistory = NormalizeExposure(history, current.preExposure);
    const auto projected = reprojection_.Run({current, std::move(normalizedHistory), motionX, motionY,
                                              confidence, cameraCut});
    result.image = projected.image;
    result.historyAccepted = projected.historyAccepted;

    result.valid = result.image.residual.size() == pixels;
    return result;
}

ScalarImage ResidualEngine::Compose(const ScalarImage& base, const ResidualImage& residual,
                                    float residualWeight) const {
    ScalarImage out = base;
    std::size_t pixels = 0;
    if (!PixelCount(base.width, base.height, pixels) || base.values.size() != pixels ||
        residual.width != base.width || residual.height != base.height ||
        residual.residual.size() != pixels || !ValidExposure(residual.preExposure) ||
        !std::isfinite(residualWeight))
        return out;

    const float weight = std::clamp(residualWeight, 0.0f, 1.0f);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float baseValue = std::isfinite(base.values[i]) ? base.values[i] : 0.0f;
        const float delta = std::isfinite(residual.residual[i]) ? residual.residual[i] : 0.0f;
        const float composed = baseValue + delta * weight;
        out.values[i] = std::isfinite(composed) ? composed : baseValue;
    }
    return out;
}

} // namespace nrfusion
