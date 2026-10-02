#include "nrfusion/PrecisionAutotuner.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

std::vector<NrPrecision> SupportedPrecisions(bool fp8, bool hybridNvfp4) {
    std::vector<NrPrecision> out;
    if (fp8) out.push_back(NrPrecision::Fp8);
    if (hybridNvfp4) out.push_back(NrPrecision::HybridNvfp4);
    return out;
}

std::optional<NrPrecision> CheaperPrecision(
    NrPrecision current, bool fp8, bool hybridNvfp4) {
    if (current == NrPrecision::Fp8)
        return fp8 && hybridNvfp4
            ? std::optional<NrPrecision>{NrPrecision::HybridNvfp4}
            : std::nullopt;
    return std::nullopt;
}

namespace {
double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}
}

PrecisionAutotuner::PrecisionAutotuner(PrecisionAutotuneConfig config) : config_(config) {
    config_.warmupSamples = std::max<std::size_t>(1, config_.warmupSamples);
    config_.measureSamples = std::max<std::size_t>(5, config_.measureSamples);
    config_.minMedianGain = std::clamp(FiniteOr(config_.minMedianGain, 0.02), 0.0, 0.50);
    config_.minAbsoluteGainMs = std::max(0.0, FiniteOr(config_.minAbsoluteGainMs, 0.05));
    config_.maxP95Regression = std::clamp(FiniteOr(config_.maxP95Regression, 0.02), 0.0, 0.50);
    Reset();
}

void PrecisionAutotuner::Reset(std::uint64_t configurationGeneration) {
    generation_ = configurationGeneration;
    state_ = PrecisionAutotuneState::BaselineWarmup;
    result_ = {};
    warmupSeen_ = 0;
    qualityEvidenceSamples_ = 0;
    qualityRejected_ = false;
    baseline_.clear();
    candidate_.clear();
    percentileScratch_.clear();
    baseline_.reserve(config_.measureSamples);
    candidate_.reserve(config_.measureSamples);
    percentileScratch_.reserve(config_.measureSamples);
}

NrPrecision PrecisionAutotuner::Desired(bool automaticEnabled, bool candidateAvailable,
                                        NrPrecision manualPrecision) const {
    if (!automaticEnabled) return manualPrecision;
    if (!candidateAvailable) return NrPrecision::Fp8;
    switch (state_) {
    case PrecisionAutotuneState::CandidateWarmup:
    case PrecisionAutotuneState::CandidateMeasure:
    case PrecisionAutotuneState::PreferNvfp4:
        return NrPrecision::HybridNvfp4;
    case PrecisionAutotuneState::BaselineWarmup:
    case PrecisionAutotuneState::BaselineMeasure:
    case PrecisionAutotuneState::PreferFp8:
    case PrecisionAutotuneState::CandidateFailed:
    default:
        return NrPrecision::Fp8;
    }
}

double PrecisionAutotuner::Percentile(
    const std::vector<double>& values, double q) {
    if (values.empty()) return 0.0;
    percentileScratch_.assign(values.begin(), values.end());
    std::sort(percentileScratch_.begin(), percentileScratch_.end());
    q = std::clamp(q, 0.0, 1.0);
    const double pos = q * static_cast<double>(percentileScratch_.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    if (lo == hi) return percentileScratch_[lo];
    const double f = pos - static_cast<double>(lo);
    return percentileScratch_[lo] * (1.0 - f) +
           percentileScratch_[hi] * f;
}

void PrecisionAutotuner::FinishCandidate() {
    result_.baselineMedianMs = Percentile(baseline_, 0.50);
    result_.candidateMedianMs = Percentile(candidate_, 0.50);
    result_.baselineP95Ms = Percentile(baseline_, 0.95);
    result_.candidateP95Ms = Percentile(candidate_, 0.95);
    if (result_.baselineMedianMs > 0.0)
        result_.medianGain = (result_.baselineMedianMs - result_.candidateMedianMs) / result_.baselineMedianMs;
    if (result_.baselineP95Ms > 0.0)
        result_.p95Regression = (result_.candidateP95Ms - result_.baselineP95Ms) / result_.baselineP95Ms;

    result_.qualityEvidenceSamples = qualityEvidenceSamples_;
    result_.qualityAccepted = !qualityRejected_ &&
                              (!config_.requireQualityEvidence ||
                               qualityEvidenceSamples_ >= candidate_.size());
    const double absoluteGain = result_.baselineMedianMs - result_.candidateMedianMs;
    result_.candidateQualified = result_.medianGain >= config_.minMedianGain &&
                                 absoluteGain >= config_.minAbsoluteGainMs &&
                                 result_.p95Regression <= config_.maxP95Regression &&
                                 result_.qualityAccepted;
    result_.finished = true;
    state_ = result_.candidateQualified ? PrecisionAutotuneState::PreferNvfp4
                                        : PrecisionAutotuneState::PreferFp8;
}

void PrecisionAutotuner::Observe(NrPrecision precision, std::uint64_t configurationGeneration,
                                 double nrGpuMs) {
    Observe(precision, configurationGeneration, nrGpuMs, QualityEvidence{});
}

void PrecisionAutotuner::Observe(NrPrecision precision, std::uint64_t configurationGeneration,
                                 double nrGpuMs, const QualityEvidence& quality) {
    if (configurationGeneration != generation_) return;
    if (!std::isfinite(nrGpuMs) || nrGpuMs <= 0.0 || nrGpuMs >= 1000.0) return;

    switch (state_) {
    case PrecisionAutotuneState::BaselineWarmup:
        if (precision != NrPrecision::Fp8) return;
        if (++warmupSeen_ >= config_.warmupSamples) {
            warmupSeen_ = 0;
            state_ = PrecisionAutotuneState::BaselineMeasure;
        }
        return;

    case PrecisionAutotuneState::BaselineMeasure:
        if (precision != NrPrecision::Fp8) return;
        baseline_.push_back(nrGpuMs);
        if (baseline_.size() >= config_.measureSamples) {
            warmupSeen_ = 0;
            state_ = PrecisionAutotuneState::CandidateWarmup;
        }
        return;

    case PrecisionAutotuneState::CandidateWarmup:
        if (precision != NrPrecision::HybridNvfp4) return; // drain delayed FP8 timings safely
        if (++warmupSeen_ >= config_.warmupSamples) {
            warmupSeen_ = 0;
            state_ = PrecisionAutotuneState::CandidateMeasure;
        }
        return;

    case PrecisionAutotuneState::CandidateMeasure: {
        if (precision != NrPrecision::HybridNvfp4) return;
        candidate_.push_back(nrGpuMs);
        // An empty verdict means that the host has no readback for this sample. It is not a
        // visual rejection: timing-only Auto remains usable. Once any verdict is supplied,
        // however, an explicit failure must not be hidden by the timing win.
        const bool qualityProvided = quality.available || quality.stable ||
                                     quality.comparedToReference || quality.numericFaults != 0 ||
                                     quality.temporalConfidence != 0.0 || quality.maxAbsError != 0.0;
        if (qualityProvided) {
            if (quality.Accepted()) ++qualityEvidenceSamples_;
            else qualityRejected_ = true;
        }
        if (candidate_.size() >= config_.measureSamples) FinishCandidate();
        return;
    }

    case PrecisionAutotuneState::PreferFp8:
    case PrecisionAutotuneState::PreferNvfp4:
    case PrecisionAutotuneState::CandidateFailed:
        return;
    }
}

void PrecisionAutotuner::ReportCandidateFailure(std::uint64_t configurationGeneration) {
    if (configurationGeneration != generation_) return;
    // Once the automatic path has selected or is testing NVFP4, any runtime failure permanently
    // falls back to FP8 for this configuration generation. A shape/WorkingScale change resets the
    // qualifier and may try again under the new workload.
    if (state_ != PrecisionAutotuneState::CandidateWarmup &&
        state_ != PrecisionAutotuneState::CandidateMeasure &&
        state_ != PrecisionAutotuneState::PreferNvfp4) return;
    result_.finished = true;
    result_.candidateQualified = false;
    result_.qualityAccepted = false;
    state_ = PrecisionAutotuneState::CandidateFailed;
}

} // namespace nrfusion
