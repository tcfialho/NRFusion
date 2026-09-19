#include "nrfusion/QualityValidator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace nrfusion {
namespace {

double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

bool PixelCount(const QualityImageView& view, std::size_t& pixels) noexcept {
    if (view.width == 0 || view.height == 0) {
        pixels = 0;
        return false;
    }
    const auto width = static_cast<std::size_t>(view.width);
    const auto height = static_cast<std::size_t>(view.height);
    if (width > (std::numeric_limits<std::size_t>::max)() / height) {
        pixels = 0;
        return false;
    }
    pixels = width * height;
    if (pixels > (std::numeric_limits<std::size_t>::max)() / 4u) {
        pixels = 0;
        return false;
    }
    return view.rgba.size() == pixels * 4u;
}

bool SameShape(const QualityImageView& left, const QualityImageView& right,
               std::size_t& pixels) noexcept {
    std::size_t leftPixels = 0;
    std::size_t rightPixels = 0;
    if (!PixelCount(left, leftPixels) || !PixelCount(right, rightPixels) ||
        left.width != right.width || left.height != right.height || leftPixels != rightPixels)
        return false;
    pixels = leftPixels;
    return true;
}

void AddFault(std::uint32_t& faults) noexcept {
    if (faults != (std::numeric_limits<std::uint32_t>::max)()) ++faults;
}

} // namespace

bool QualityImageView::Valid() const noexcept {
    std::size_t pixels = 0;
    return PixelCount(*this, pixels);
}

QualityValidator::QualityValidator(QualityValidatorConfig config) : config_(config) {
    config_.maxMeanAbsError = std::max(0.0, FiniteOr(config_.maxMeanAbsError, 0.02));
    config_.maxP95AbsError = std::max(config_.maxMeanAbsError,
                                      FiniteOr(config_.maxP95AbsError, 0.05));
    config_.maxAbsError = std::max(config_.maxP95AbsError,
                                   FiniteOr(config_.maxAbsError, 0.25));
    config_.maxTemporalError = std::max(0.0, FiniteOr(config_.maxTemporalError, 0.05));
}

double QualityValidator::Percentile(std::span<const double> values, double q) {
    if (values.empty()) return 0.0;
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    q = std::clamp(std::isfinite(q) ? q : 0.0, 0.0, 1.0);
    const double position = q * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) return sorted[lower];
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - fraction) + sorted[upper] * fraction;
}

QualityValidationResult QualityValidator::Compare(
    const QualityImageView& reference,
    const QualityImageView& candidate,
    const QualityImageView* previousReference,
    const QualityImageView* previousCandidate) const {
    QualityValidationResult result;
    std::size_t pixels = 0;
    if (!SameShape(reference, candidate, pixels)) return result;

    std::vector<double> spatialErrors;
    spatialErrors.reserve(pixels);
    std::uint32_t numericFaults = 0;
    double spatialSum = 0.0;
    double spatialMax = 0.0;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        double pixelError = 0.0;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const float referenceValue = reference.rgba[pixel * 4u + channel];
            const float candidateValue = candidate.rgba[pixel * 4u + channel];
            if (!std::isfinite(referenceValue) || !std::isfinite(candidateValue)) {
                AddFault(numericFaults);
                continue;
            }
            pixelError = std::max(pixelError, std::fabs(static_cast<double>(candidateValue) -
                                                        static_cast<double>(referenceValue)));
        }
        spatialErrors.push_back(pixelError);
        spatialSum += pixelError;
        spatialMax = std::max(spatialMax, pixelError);
    }

    result.meanAbsError = spatialSum / static_cast<double>(pixels);
    result.p95AbsError = Percentile(spatialErrors, 0.95);
    result.maxAbsError = spatialMax;

    if (previousReference != nullptr && previousCandidate != nullptr) {
        std::size_t previousPixels = 0;
        if (SameShape(*previousReference, *previousCandidate, previousPixels) &&
            previousPixels == pixels && previousReference->width == reference.width &&
            previousReference->height == reference.height) {
            std::vector<double> temporalErrors;
            temporalErrors.reserve(pixels);
            for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
                double pixelError = 0.0;
                for (std::size_t channel = 0; channel < 4; ++channel) {
                    const std::size_t offset = pixel * 4u + channel;
                    const float referenceDelta = reference.rgba[offset] - previousReference->rgba[offset];
                    const float candidateDelta = candidate.rgba[offset] - previousCandidate->rgba[offset];
                    if (!std::isfinite(referenceDelta) || !std::isfinite(candidateDelta)) {
                        AddFault(numericFaults);
                        continue;
                    }
                    pixelError = std::max(pixelError,
                                          std::fabs(static_cast<double>(candidateDelta) -
                                                    static_cast<double>(referenceDelta)));
                }
                temporalErrors.push_back(pixelError);
            }
            result.temporalCompared = true;
            result.temporalP95Error = Percentile(temporalErrors, 0.95);
        }
    }

    result.evidence.available = true;
    result.evidence.comparedToReference = true;
    result.evidence.temporalConfidence = result.temporalCompared
        ? std::clamp(1.0 - result.temporalP95Error /
                         std::max(config_.maxTemporalError, 1.0e-9), 0.0, 1.0)
        : 1.0;
    result.evidence.maxAbsError = result.maxAbsError;
    result.evidence.numericFaults = numericFaults;
    result.evidence.stable = numericFaults == 0 &&
                             result.meanAbsError <= config_.maxMeanAbsError &&
                             result.p95AbsError <= config_.maxP95AbsError &&
                             result.maxAbsError <= config_.maxAbsError &&
                             (!result.temporalCompared ||
                              result.temporalP95Error <= config_.maxTemporalError);
    return result;
}

} // namespace nrfusion
