#pragma once

#include "nrfusion/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace nrfusion {

// Host-provided readback view. Values are linear RGBA floats in the same dimensions and frame
// domain; the validator never converts presentation images or invents a reference frame.
struct QualityImageView {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const float> rgba{};

    bool Valid() const noexcept;
};

struct QualityValidatorConfig {
    double maxMeanAbsError = 0.02;
    double maxP95AbsError = 0.05;
    double maxAbsError = 0.25;
    double maxTemporalError = 0.05;
};

struct QualityValidationResult {
    QualityEvidence evidence{};
    double meanAbsError = 0.0;
    double p95AbsError = 0.0;
    double maxAbsError = 0.0;
    double temporalP95Error = 0.0;
    bool temporalCompared = false;
};

// Compares a candidate output with a same-frame reference. The previous pair is optional; when
// present, temporal motion of the candidate is compared with the reference motion so a static spatial
// match cannot hide ghosting or smearing. This is an evidence producer, not a quality claim by itself.
class QualityValidator {
public:
    explicit QualityValidator(QualityValidatorConfig config = {});

    QualityValidationResult Compare(
        const QualityImageView& reference,
        const QualityImageView& candidate,
        const QualityImageView* previousReference = nullptr,
        const QualityImageView* previousCandidate = nullptr) const;

    const QualityValidatorConfig& Config() const noexcept { return config_; }

private:
    static double Percentile(std::span<const double> values, double q);

    QualityValidatorConfig config_;
};

} // namespace nrfusion
