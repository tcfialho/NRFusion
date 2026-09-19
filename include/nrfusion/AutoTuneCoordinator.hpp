#pragma once
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/Types.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace nrfusion {

enum class AutoTuneObjective : std::uint8_t {
    HighestQualityAtTarget,
    LowestCriticalPath
};

struct AutoTuneConfig {
    double targetFps = 120.0;
    std::size_t warmupSamples = 12;
    std::size_t measureSamples = 36;
    double maxQueuePressure = 0.78;
    double frameBudgetTolerance = 1.03;
    AutoTuneObjective objective = AutoTuneObjective::HighestQualityAtTarget;
    std::vector<float> scaleSteps {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f, 0.42f};
};

struct AutoTuneCandidate {
    float workingScale = 1.0f;
    SchedulerMode scheduler = SchedulerMode::Serialized;
    NrPrecision precision = NrPrecision::Fp8;
};

struct AutoTuneMeasurement {
    AutoTuneCandidate candidate{};
    double medianFrameMs = 0.0;
    double p95FrameMs = 0.0;
    double medianNrMs = 0.0;
    double meanQueuePressure = 0.0;
    bool targetMet = false;
    bool rejected = false;
};

enum class AutoTuneState : std::uint8_t {
    Idle,
    Warmup,
    Measure,
    Finished
};

class AutoTuneCoordinator {
public:
    explicit AutoTuneCoordinator(AutoTuneConfig config = {});

    void Start(const RuntimeCapabilities& capabilities, float minScale = 0.42f,
               float maxScale = 1.0f);
    void Observe(const TelemetrySample& sample);
    void RejectCurrent();
    void Reset();

    AutoTuneState State() const noexcept { return state_; }
    std::optional<AutoTuneCandidate> Current() const;
    std::optional<AutoTuneMeasurement> Best() const;
    const std::vector<AutoTuneMeasurement>& Results() const noexcept { return results_; }

private:
    static double Percentile(std::vector<double> values, double q);
    void FinishCurrent(bool rejected);
    void Advance();
    std::optional<std::size_t> BestIndex() const;

    AutoTuneConfig config_;
    AutoTuneState state_ = AutoTuneState::Idle;
    std::vector<AutoTuneCandidate> candidates_;
    std::vector<AutoTuneMeasurement> results_;
    std::size_t candidateIndex_ = 0;
    std::size_t warmupSeen_ = 0;
    std::vector<double> frameMs_;
    std::vector<double> nrMs_;
    std::vector<double> queuePressure_;
};

} // namespace nrfusion
