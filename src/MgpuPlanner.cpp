#include "nrfusion/MgpuPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {

constexpr double kRejectedCostMs = 1e9;

double SaturatingCost(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 ? std::min(value, kRejectedCostMs) : kRejectedCostMs;
}

double TransferMs(std::uint64_t bytes, double gbps) {
    if (bytes == 0) return 0.0;
    if (!std::isfinite(gbps) || gbps <= 0.0) return kRejectedCostMs;
    return SaturatingCost((static_cast<double>(bytes) / (gbps * 1'000'000'000.0)) * 1000.0);
}

double FiniteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

std::uint64_t SaturatingMultiply(std::uint64_t a, std::uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > std::numeric_limits<std::uint64_t>::max() / b)
        return std::numeric_limits<std::uint64_t>::max();
    return a * b;
}

} // namespace

MgpuPlan MgpuPlanner::Plan(const MgpuInput& i) const {
    MgpuPlan p{};
    if (i.renderWidth == 0 || i.renderHeight == 0) return p;
    const double scale = std::clamp(FiniteOr(i.workingScale, 1.0), 0.25, 1.0);
    p.workWidth = std::max(1u, static_cast<std::uint32_t>(std::llround(i.renderWidth * scale)));
    p.workHeight = std::max(1u, static_cast<std::uint32_t>(std::llround(i.renderHeight * scale)));

    const std::uint64_t pixels = static_cast<std::uint64_t>(p.workWidth) * p.workHeight;
    const std::uint64_t uploadBytes = SaturatingMultiply(pixels, i.bytesPerInputPixel);
    const std::uint64_t returnBytes = SaturatingMultiply(pixels, i.bytesPerResidualPixel);

    const double fixedSyncMs = std::max(0.0, FiniteOr(i.fixedSyncMs, 0.20));
    const double primaryNrMs = std::max(0.0, FiniteOr(i.primaryNrMs, 0.0));
    const double secondaryNrMs = std::max(0.0, FiniteOr(i.secondaryNrMs, 0.0));
    p.estimatedTransferMs = SaturatingCost(TransferMs(uploadBytes, i.uploadGbps) +
                                           TransferMs(returnBytes, i.downloadGbps) + fixedSyncMs);
    p.estimatedCriticalMs = SaturatingCost(secondaryNrMs + p.estimatedTransferMs);
    p.estimatedGainMs = SaturatingCost(std::max(0.0, primaryNrMs - p.estimatedCriticalMs));

    // Require useful margin; cross-adapter jitter makes a mathematically tiny win undesirable.
    const bool meaningfulGain = p.estimatedGainMs >= std::max(0.35, primaryNrMs * 0.12);
    p.useSecondary = i.available && i.stable && primaryNrMs > 0.0 && secondaryNrMs > 0.0 && meaningfulGain;
    return p;
}

} // namespace nrfusion
