#include "nrfusion/NrCostModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {

namespace {
constexpr float kScaleEpsilon = 0.0025f;
constexpr std::size_t kMaxWeightPerScale = 24;
constexpr std::size_t kMaxSampleAge = 240;
constexpr std::size_t kMinSamplesPerScale = 3;
}

void NrCostModel::ReserveScales(std::size_t count) {
    points_.reserve(count);
}

void NrCostModel::Reset() {
    points_.clear();
    totalSamples_ = 0;
}

NrCostModel::Point* NrCostModel::FindPoint(float scale) {
    for (auto& point : points_) {
        if (std::fabs(point.scale - scale) <= kScaleEpsilon) return &point;
    }
    return nullptr;
}

const NrCostModel::Point* NrCostModel::FindPoint(float scale) const {
    for (const auto& point : points_) {
        if (std::fabs(point.scale - scale) <= kScaleEpsilon) return &point;
    }
    return nullptr;
}

void NrCostModel::Observe(float scale, double gpuMs) {
    if (!std::isfinite(scale) || scale <= 0.0f || scale > 2.0f ||
        !std::isfinite(gpuMs) || gpuMs <= 0.0 || gpuMs >= 1000.0)
        return;

    // Serial age is relative only. On exhaustion, discard the tiny learned model instead of
    // wrapping and making ancient rungs appear fresh again.
    if (totalSamples_ == std::numeric_limits<std::size_t>::max()) Reset();

    Point* point = FindPoint(scale);
    if (point == nullptr) {
        points_.push_back(Point{scale, gpuMs, 1, totalSamples_ + 1});
    } else {
        if (point->count >= 3 && (gpuMs > point->meanMs * 2.5 || gpuMs < point->meanMs * 0.40))
            return; // do not train on a one-frame clock/stall outlier
        // Per-rung running mean. Cap its effective inertia so a driver/clock-state change can
        // eventually replace old observations instead of remaining anchored forever.
        const std::size_t effectiveCount = std::min(point->count, kMaxWeightPerScale);
        point->meanMs += (gpuMs - point->meanMs) / static_cast<double>(effectiveCount + 1);
        if (point->count < kMaxWeightPerScale) ++point->count;
        point->lastSerial = totalSamples_ + 1;
    }
    ++totalSamples_;
}

std::optional<NrCostFit> NrCostModel::Fit() const {
    std::size_t activePoints = 0;
    for (const auto& p : points_)
        if (p.count >= kMinSamplesPerScale && totalSamples_ - p.lastSerial <= kMaxSampleAge) ++activePoints;
    if (activePoints < 2) return std::nullopt;

    double sw = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    std::size_t samples = 0;

    for (const auto& p : points_) {
        if (p.count < kMinSamplesPerScale || totalSamples_ - p.lastSerial > kMaxSampleAge) continue;
        const double x = static_cast<double>(p.scale) * static_cast<double>(p.scale);
        const double y = p.meanMs;
        const double w = static_cast<double>(std::min(p.count, kMaxWeightPerScale));
        sw += w;
        sx += w * x;
        sy += w * y;
        sxx += w * x * x;
        sxy += w * x * y;
        samples += p.count;
    }

    const double denom = sw * sxx - sx * sx;
    if (sw <= 0.0 || denom <= 1e-9) return std::nullopt;

    double areaMs = (sw * sxy - sx * sy) / denom;
    double fixedMs = (sy - areaMs * sx) / sw;
    if (!std::isfinite(areaMs) || !std::isfinite(fixedMs) || areaMs <= 0.05)
        return std::nullopt;

    // A negative fixed term is normally noise from only a few rungs. Clamp it to zero and
    // refit only the area coefficient through the origin; never predict "negative overhead".
    if (fixedMs < 0.0) {
        fixedMs = 0.0;
        double wx2 = 0.0;
        double wxy = 0.0;
        for (const auto& p : points_) {
            if (p.count < kMinSamplesPerScale || totalSamples_ - p.lastSerial > kMaxSampleAge) continue;
            const double x = static_cast<double>(p.scale) * static_cast<double>(p.scale);
            const double w = static_cast<double>(std::min(p.count, kMaxWeightPerScale));
            wx2 += w * x * x;
            wxy += w * x * p.meanMs;
        }
        if (wx2 <= 1e-9) return std::nullopt;
        areaMs = wxy / wx2;
    }

    // A fitted fixed cost larger than every observed cost is physically useless for prediction.
    double minObserved = 1000.0;
    for (const auto& p : points_) {
        if (p.count >= kMinSamplesPerScale && totalSamples_ - p.lastSerial <= kMaxSampleAge)
            minObserved = std::min(minObserved, p.meanMs);
    }
    if (fixedMs >= minObserved * 0.98) return std::nullopt;

    const double meanY = sy / sw;
    double sse = 0.0;
    double sst = 0.0;
    for (const auto& p : points_) {
        if (p.count < kMinSamplesPerScale || totalSamples_ - p.lastSerial > kMaxSampleAge) continue;
        const double x = static_cast<double>(p.scale) * static_cast<double>(p.scale);
        const double pred = fixedMs + areaMs * x;
        const double w = static_cast<double>(std::min(p.count, kMaxWeightPerScale));
        const double err = p.meanMs - pred;
        const double dev = p.meanMs - meanY;
        sse += w * err * err;
        sst += w * dev * dev;
    }
    const double r2 = sst > 1e-9 ? std::clamp(1.0 - sse / sst, 0.0, 1.0) : 1.0;

    // Two rungs are enough to identify the model, but demand a very clean fit. With three or more,
    // tolerate some noise while still refusing a model that explains little of the scale trend.
    const double minR2 = activePoints == 2 ? 0.995 : 0.80;
    if (r2 < minR2) return std::nullopt;

    return NrCostFit{fixedMs, areaMs, r2, activePoints, samples};
}

std::optional<float> NrCostModel::PredictScaleForCost(double targetGpuMs,
                                                       float minScale,
                                                       float maxScale) const {
    if (!std::isfinite(targetGpuMs) || targetGpuMs <= 0.0 ||
        !std::isfinite(minScale) || !std::isfinite(maxScale))
        return std::nullopt;

    minScale = std::clamp(minScale, 0.25f, 2.0f);
    maxScale = std::clamp(maxScale, minScale, 2.0f);
    const auto fit = Fit();
    if (!fit) return std::nullopt;

    if (targetGpuMs <= fit->fixedMs + 1e-6)
        return minScale;

    const double area = (targetGpuMs - fit->fixedMs) / fit->areaMs;
    if (!std::isfinite(area) || area <= 0.0) return minScale;
    return std::clamp(static_cast<float>(std::sqrt(area)), minScale, maxScale);
}

} // namespace nrfusion
