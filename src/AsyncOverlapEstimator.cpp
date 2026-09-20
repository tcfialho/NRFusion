#include "nrfusion/AsyncOverlapEstimator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nrfusion {
namespace {

void SortInline(GpuInterval* intervals, std::size_t count) {
    for (std::size_t i = 1; i < count; ++i) {
        const GpuInterval key = intervals[i];
        std::size_t j = i;
        while (j > 0 && intervals[j - 1].beginMs > key.beginMs) {
            intervals[j] = intervals[j - 1];
            --j;
        }
        intervals[j] = key;
    }
}

double CoveredFractionSorted(const GpuInterval& nr, const GpuInterval* clipped, std::size_t count) {
    if (count == 0) return 0.0;
    double covered = 0.0;
    double begin = clipped[0].beginMs;
    double end = clipped[0].endMs;
    for (std::size_t i = 1; i < count; ++i) {
        if (clipped[i].beginMs <= end) end = std::max(end, clipped[i].endMs);
        else {
            covered += end - begin;
            begin = clipped[i].beginMs;
            end = clipped[i].endMs;
        }
    }
    covered += end - begin;
    return std::clamp(covered / (nr.endMs - nr.beginMs), 0.0, 1.0);
}

}

AsyncOverlapEstimator::AsyncOverlapEstimator(double smoothingSeconds)
    : smoothingSeconds_(std::max(0.01, std::isfinite(smoothingSeconds) ? smoothingSeconds : 0.75)) {}

double AsyncOverlapEstimator::Instantaneous(const GpuInterval& nr, const std::vector<GpuInterval>& concurrent) {
    if (!std::isfinite(nr.beginMs) || !std::isfinite(nr.endMs) || nr.endMs <= nr.beginMs) return 0.0;

    constexpr std::size_t kInlineIntervals = 16;
    if (concurrent.size() > kInlineIntervals) {
        std::vector<GpuInterval> clipped;
        clipped.reserve(concurrent.size());
        for (const auto& i : concurrent) {
            if (!std::isfinite(i.beginMs) || !std::isfinite(i.endMs) || i.endMs <= i.beginMs) continue;
            const double b = std::max(nr.beginMs, i.beginMs);
            const double e = std::min(nr.endMs, i.endMs);
            if (e > b) clipped.push_back({b, e});
        }
        std::sort(clipped.begin(), clipped.end(),
                  [](const auto& a, const auto& b) { return a.beginMs < b.beginMs; });
        return CoveredFractionSorted(nr, clipped.data(), clipped.size());
    }

    std::array<GpuInterval, kInlineIntervals> clipped{};
    std::size_t count = 0;
    for (const auto& i : concurrent) {
        if (!std::isfinite(i.beginMs) || !std::isfinite(i.endMs) || i.endMs <= i.beginMs) continue;
        const double b = std::max(nr.beginMs, i.beginMs);
        const double e = std::min(nr.endMs, i.endMs);
        if (e > b) clipped[count++] = {b, e};
    }
    SortInline(clipped.data(), count);
    return CoveredFractionSorted(nr, clipped.data(), count);
}

double AsyncOverlapEstimator::Update(const GpuInterval& nr, const std::vector<GpuInterval>& concurrent, double dtSeconds) {
    const double instant = Instantaneous(nr, concurrent);
    const double dt = std::clamp(std::isfinite(dtSeconds) ? dtSeconds : 0.0, 0.0, 1.0);
    if (!initialized_) { value_ = instant; initialized_ = true; return value_; }
    const double alpha = dt > 0.0 ? 1.0 - std::exp(-dt / smoothingSeconds_) : 0.05;
    value_ += alpha * (instant - value_);
    return value_;
}

} // namespace nrfusion
