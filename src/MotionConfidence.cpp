#include "nrfusion/MotionConfidence.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace nrfusion {
namespace {

bool PixelCount(std::uint32_t width, std::uint32_t height, std::size_t& count) noexcept {
    if (width == 0 || height == 0) { count = 0; return false; }
    const std::size_t w = static_cast<std::size_t>(width);
    if (w > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) return false;
    count = w * static_cast<std::size_t>(height);
    return true;
}

std::optional<float> Bilinear(const std::vector<float>& values, std::uint32_t width,
                              std::uint32_t height, double x, double y) noexcept {
    std::size_t count = 0;
    if (!PixelCount(width, height, count) || values.size() != count || !std::isfinite(x) ||
        !std::isfinite(y) || x < 0.0 || y < 0.0 || x > static_cast<double>(width - 1) ||
        y > static_cast<double>(height - 1))
        return std::nullopt;
    const auto x0 = static_cast<std::uint32_t>(std::floor(x));
    const auto y0 = static_cast<std::uint32_t>(std::floor(y));
    const auto x1 = x0 < width - 1 ? x0 + 1 : x0;
    const auto y1 = y0 < height - 1 ? y0 + 1 : y0;
    const double fx = x - static_cast<double>(x0);
    const double fy = y - static_cast<double>(y0);
    const std::size_t row0 = static_cast<std::size_t>(y0) * width;
    const std::size_t row1 = static_cast<std::size_t>(y1) * width;
    const double v00 = static_cast<double>(values[row0 + x0]);
    const double v10 = static_cast<double>(values[row0 + x1]);
    const double v01 = static_cast<double>(values[row1 + x0]);
    const double v11 = static_cast<double>(values[row1 + x1]);
    if (!std::isfinite(v00) || !std::isfinite(v10) || !std::isfinite(v01) || !std::isfinite(v11))
        return std::nullopt;
    const double top = v00 * (1.0 - fx) + v10 * fx;
    const double bottom = v01 * (1.0 - fx) + v11 * fx;
    const double result = top * (1.0 - fy) + bottom * fy;
    if (!std::isfinite(result) || std::fabs(result) > static_cast<double>(std::numeric_limits<float>::max()))
        return std::nullopt;
    return static_cast<float>(result);
}

float Agreement(double error, double threshold) noexcept {
    if (!std::isfinite(error) || error < 0.0 || !std::isfinite(threshold) || threshold < 0.0)
        return 0.0f;
    if (threshold == 0.0) return error == 0.0 ? 1.0f : 0.0f;
    return static_cast<float>(std::clamp(1.0 - error / threshold, 0.0, 1.0));
}

} // namespace

MotionConfidenceEngine::MotionConfidenceEngine(MotionConfidenceConfig config) : config_(config) {
    if (!std::isfinite(config_.maxConsistencyErrorPixels)) config_.maxConsistencyErrorPixels = 2.0f;
    if (!std::isfinite(config_.depthRelativeThreshold)) config_.depthRelativeThreshold = 0.04f;
    if (!std::isfinite(config_.maxMotionPixels)) config_.maxMotionPixels = 512.0f;
    config_.maxConsistencyErrorPixels = std::max(0.0f, config_.maxConsistencyErrorPixels);
    config_.depthRelativeThreshold = std::max(0.0f, config_.depthRelativeThreshold);
    config_.maxMotionPixels = std::max(0.0f, config_.maxMotionPixels);
}

MotionConfidenceResult MotionConfidenceEngine::Evaluate(const MotionConfidenceInput& in) const {
    MotionConfidenceResult out;
    std::size_t count = 0;
    if (!PixelCount(in.width, in.height, count) || in.forwardX.size() != count ||
        in.forwardY.size() != count || in.backwardX.size() != count ||
        in.backwardY.size() != count || in.currentDepth.size() != count ||
        in.historyDepth.size() != count)
        return out;

    out.validShape = true;
    out.confidence.assign(count, 0.0f);
    out.valid.assign(count, 0u);
    if (in.cameraCut) return out;

    std::size_t validCount = 0;
    double confidenceSum = 0.0;
    double fbSum = 0.0;
    double depthSum = 0.0;
    for (std::uint32_t y = 0; y < in.height; ++y) {
        for (std::uint32_t x = 0; x < in.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * in.width + x;
            const double fx = static_cast<double>(in.forwardX[i]);
            const double fy = static_cast<double>(in.forwardY[i]);
            const double currentDepth = static_cast<double>(in.currentDepth[i]);
            if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(currentDepth) ||
                std::hypot(fx, fy) > static_cast<double>(config_.maxMotionPixels))
                continue;

            const double hx = static_cast<double>(x) - fx;
            const double hy = static_cast<double>(y) - fy;
            const auto bx = Bilinear(in.backwardX, in.width, in.height, hx, hy);
            const auto by = Bilinear(in.backwardY, in.width, in.height, hx, hy);
            const auto historyDepth = Bilinear(in.historyDepth, in.width, in.height, hx, hy);
            if (!bx || !by || !historyDepth) continue;

            const double consistencyError = std::hypot(fx + static_cast<double>(*bx),
                                                       fy + static_cast<double>(*by));
            const float fb = Agreement(consistencyError, static_cast<double>(config_.maxConsistencyErrorPixels));
            const double depthScale = std::max(1.0, std::fabs(currentDepth));
            const double depthLimit = static_cast<double>(config_.depthRelativeThreshold) * depthScale;
            const float depth = Agreement(std::fabs(currentDepth - static_cast<double>(*historyDepth)),
                                          depthLimit);
            const float confidence = fb * depth;
            if (!(confidence > 0.0f) || !std::isfinite(confidence)) continue;

            out.confidence[i] = confidence;
            out.valid[i] = 1u;
            ++validCount;
            confidenceSum += static_cast<double>(confidence);
            fbSum += static_cast<double>(fb);
            depthSum += static_cast<double>(depth);
        }
    }

    out.validPixelRatio = static_cast<double>(validCount) / static_cast<double>(count);
    if (validCount != 0) {
        const double denom = static_cast<double>(validCount);
        out.meanConfidence = confidenceSum / denom;
        out.forwardBackwardAgreement = fbSum / denom;
        out.depthAgreement = depthSum / denom;
    }
    return out;
}

} // namespace nrfusion
