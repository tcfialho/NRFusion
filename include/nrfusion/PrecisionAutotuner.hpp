#pragma once

#include "nrfusion/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace nrfusion {

enum class NrPrecision : std::uint8_t {
    Fp8 = 0,
    HybridNvfp4 = 4,
};

// Which precisions a given GPU can actually run. A menu must offer only these, and Auto may pick
// only from these: a format the hardware lacks is not a slower option, it is a dead control.
// Ordered cheapest-last so index 0 is always the fidelity baseline.
std::vector<NrPrecision> SupportedPrecisions(bool fp8, bool hybridNvfp4);

// The next cheaper precision after `current`, or nothing when `current` is already the cheapest
// the hardware offers. This is what lets a controller trade precision before it trades resolution.
std::optional<NrPrecision> CheaperPrecision(NrPrecision current, bool fp8, bool hybridNvfp4);

enum class PrecisionAutotuneState : std::uint8_t {
    BaselineWarmup,
    BaselineMeasure,
    CandidateWarmup,
    CandidateMeasure,
    PreferFp8,
    PreferNvfp4,
    CandidateFailed,
};

struct PrecisionAutotuneConfig {
    std::size_t warmupSamples = 12;
    std::size_t measureSamples = 36;
    // GPU timing is the primary Auto signal for a performance optimization. A host may still
    // provide same-workload visual evidence; when requireQualityEvidence is enabled every
    // candidate sample must carry an accepted verdict. With the default disabled, missing
    // readback does not block a clear timing win, while an explicitly rejected verdict still
    // vetoes the candidate.
    bool requireQualityEvidence = false;
    double minMedianGain = 0.02;       // require at least 2% median pass-time win
    double minAbsoluteGainMs = 0.05;   // ignore wins smaller than timer/noise scale
    double maxP95Regression = 0.02;    // tail may not get more than 2% worse
};

struct PrecisionAutotuneResult {
    bool finished = false;
    bool candidateQualified = false;
    double baselineMedianMs = 0.0;
    double candidateMedianMs = 0.0;
    double baselineP95Ms = 0.0;
    double candidateP95Ms = 0.0;
    double medianGain = 0.0;
    double p95Regression = 0.0;
    std::size_t qualityEvidenceSamples = 0;
    bool qualityAccepted = false;
};

// Runtime precision qualifier. It is deliberately driven by completed GPU timings rather than CPU
// frame counts: feature rebuild/warm-up does not advance a phase until actual NR work has retired.
class PrecisionAutotuner {
public:
    explicit PrecisionAutotuner(PrecisionAutotuneConfig config = {});

    void Reset(std::uint64_t configurationGeneration = 0);
    NrPrecision Desired(bool automaticEnabled, bool candidateAvailable,
                        NrPrecision manualPrecision = NrPrecision::Fp8) const;
    void Observe(NrPrecision precision, std::uint64_t configurationGeneration, double nrGpuMs);
    void Observe(NrPrecision precision, std::uint64_t configurationGeneration, double nrGpuMs,
                 const QualityEvidence& quality);
    void ReportCandidateFailure(std::uint64_t configurationGeneration);

    PrecisionAutotuneState State() const noexcept { return state_; }
    PrecisionAutotuneResult Result() const noexcept { return result_; }
    std::uint64_t ConfigurationGeneration() const noexcept { return generation_; }

private:
    static double Percentile(std::vector<double> values, double q);
    void FinishCandidate();

    PrecisionAutotuneConfig config_;
    PrecisionAutotuneState state_ = PrecisionAutotuneState::BaselineWarmup;
    PrecisionAutotuneResult result_{};
    std::uint64_t generation_ = 0;
    std::size_t warmupSeen_ = 0;
    std::size_t qualityEvidenceSamples_ = 0;
    bool qualityRejected_ = false;
    std::vector<double> baseline_;
    std::vector<double> candidate_;
};

} // namespace nrfusion
