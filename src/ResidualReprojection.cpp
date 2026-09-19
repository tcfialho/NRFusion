#include "nrfusion/ResidualReprojection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {
bool PixelCount(std::uint32_t w, std::uint32_t h, std::size_t& count) {
    if (w == 0 || h == 0) {
        count = 0;
        return true;
    }
    const std::size_t width = static_cast<std::size_t>(w);
    if (width > std::numeric_limits<std::size_t>::max() / h) return false;
    count = width * static_cast<std::size_t>(h);
    return true;
}

float Bilinear(const std::vector<float>& v, std::uint32_t w, std::uint32_t h, float x, float y) {
    std::size_t pixels = 0;
    if (!PixelCount(w, h, pixels) || v.size() != pixels || pixels == 0 ||
        !std::isfinite(x) || !std::isfinite(y))
        return 0.0f;

    // Clamp in double precision: float cannot represent every uint32 coordinate near UINT32_MAX.
    const double xd = std::clamp(static_cast<double>(x), 0.0, static_cast<double>(w - 1));
    const double yd = std::clamp(static_cast<double>(y), 0.0, static_cast<double>(h - 1));
    const auto x0 = static_cast<std::uint32_t>(std::floor(xd));
    const auto y0 = static_cast<std::uint32_t>(std::floor(yd));
    const auto x1 = x0 < w - 1 ? x0 + 1 : x0;
    const auto y1 = y0 < h - 1 ? y0 + 1 : y0;
    const float fx = static_cast<float>(xd - static_cast<double>(x0));
    const float fy = static_cast<float>(yd - static_cast<double>(y0));
    const std::size_t row0 = static_cast<std::size_t>(y0) * w;
    const std::size_t row1 = static_cast<std::size_t>(y1) * w;
    const float a = v[row0 + x0] * (1.0f-fx) + v[row0 + x1] * fx;
    const float b = v[row1 + x0] * (1.0f-fx) + v[row1 + x1] * fx;
    return a * (1.0f-fy) + b * fy;
}
}

ResidualReprojection::ResidualReprojection(ResidualReprojectionConfig config) : config_(config) {
    if (!std::isfinite(config_.currentBlend)) config_.currentBlend = 0.08f;
    if (!std::isfinite(config_.minConfidence)) config_.minConfidence = 0.25f;
    if (!std::isfinite(config_.depthRelativeThreshold)) config_.depthRelativeThreshold = 0.04f;
    if (!std::isfinite(config_.maxMotionPixels)) config_.maxMotionPixels = 512.0f;
    config_.currentBlend = std::clamp(config_.currentBlend, 0.0f, 1.0f);
    config_.minConfidence = std::clamp(config_.minConfidence, 0.0f, 1.0f);
    config_.depthRelativeThreshold = std::max(0.0f, config_.depthRelativeThreshold);
    config_.maxMotionPixels = std::max(0.0f, config_.maxMotionPixels);
}

ResidualReprojectionOutput ResidualReprojection::Run(const ResidualReprojectionInput& in) const {
    ResidualReprojectionOutput out;
    const auto w = in.current.width, h = in.current.height;
    out.image.width = w; out.image.height = h;
    const bool currentExposureValid = std::isfinite(in.current.preExposure) && in.current.preExposure > 0.0f;
    const bool historyExposureValid = std::isfinite(in.history.preExposure) && in.history.preExposure > 0.0f;
    const float exposureScale = currentExposureValid && historyExposureValid
        ? std::max({1.0f, std::fabs(in.current.preExposure), std::fabs(in.history.preExposure)})
        : 1.0f;
    const bool sameExposureDomain = currentExposureValid && historyExposureValid &&
        std::fabs(in.current.preExposure - in.history.preExposure) <= 1.0e-5f * exposureScale;
    out.image.preExposure = currentExposureValid ? in.current.preExposure : 0.0f;
    out.image.sourceFrame = in.current.sourceFrame;
    std::size_t n = 0;
    if (!PixelCount(w, h, n)) return out;
    out.image.residual = in.current.residual;
    if (out.image.residual.size() != n) out.image.residual.assign(n, 0.0f);
    else for (float& v : out.image.residual) if (!std::isfinite(v)) v = 0.0f;
    out.image.depth = in.current.depth;
    out.historyAccepted.assign(n, 0);
    // History reuse is fail-closed: every temporal guide required by ResidualValidity must exist,
    // and source-frame identity must prove that history is strictly older than the current frame.
    if (!sameExposureDomain || in.cameraCut || w == 0 || h == 0 ||
        in.history.width != w || in.history.height != h || in.history.residual.size() != n ||
        in.current.depth.size() != n || in.history.depth.size() != n ||
        in.motionX.size() != n || in.motionY.size() != n || in.confidence.size() != n ||
        in.current.sourceFrame == 0 || in.history.sourceFrame == 0 ||
        in.history.sourceFrame >= in.current.sourceFrame) return out;

    const float currentBlend = std::clamp(config_.currentBlend, 0.0f, 1.0f);
    for (std::uint32_t y = 0; y < h; ++y) for (std::uint32_t x = 0; x < w; ++x) {
        const std::size_t i = static_cast<std::size_t>(y) * w + x;
        const float mx = in.motionX[i], my = in.motionY[i];
        const float rawConfidence = in.confidence[i];
        if (!std::isfinite(mx) || !std::isfinite(my) || !std::isfinite(rawConfidence) ||
            std::hypot(mx, my) > config_.maxMotionPixels) continue;
        const float conf = std::clamp(rawConfidence, 0.0f, 1.0f);
        if (conf < config_.minConfidence) continue;
        const float hx = static_cast<float>(x) - mx, hy = static_cast<float>(y) - my;
        if (hx < 0.0f || hy < 0.0f || hx > static_cast<float>(w - 1) || hy > static_cast<float>(h - 1)) continue;
        const float cd = in.current.depth[i];
        const float hd = Bilinear(in.history.depth, w, h, hx, hy);
        if (!std::isfinite(cd) || !std::isfinite(hd)) continue;
        const float scale = std::max(1.0f, std::fabs(cd));
        if (std::fabs(cd - hd) > config_.depthRelativeThreshold * scale) continue;
        const float hr = Bilinear(in.history.residual, w, h, hx, hy);
        if (!std::isfinite(hr)) continue;
        const float historyWeight = (1.0f - currentBlend) * conf;
        const float cur = out.image.residual[i];
        const float blended = cur * (1.0f - historyWeight) + hr * historyWeight;
        if (!std::isfinite(blended)) continue;
        out.image.residual[i] = blended;
        out.historyAccepted[i] = 1;
    }
    return out;
}

} // namespace nrfusion
