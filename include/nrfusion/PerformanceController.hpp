#pragma once

#include "nrfusion/Types.hpp"
#include "nrfusion/NrCostModel.hpp"
#include <cstddef>
#include <optional>
#include <vector>

namespace nrfusion {

enum class AdaptiveState : std::uint8_t {
    WaitingForTiming,
    Stable,
    ReducingScale,
    ExternalPressure,
    RecoveringQuality
};

struct PerformanceConfig {
    double targetFps = 120.0;

    // NR budget can follow the user's frame target. Example: 120 FPS => 8.33 ms frame budget;
    // at 30% the controller tries to keep critical NR at roughly 2.5 ms.
    bool automaticNrBudget = true;
    double nrBudgetMs = 3.0;              // used only when automaticNrBudget=false
    double nrBudgetFrameFraction = 0.30;
    double minNrBudgetMs = 0.9;
    double maxNrBudgetMs = 5.0;

    double scaleDownSustainSeconds = 0.75;
    double scaleUpSustainSeconds = 4.0;
    double cooldownSeconds = 2.5;
    // A new scale is judged from a short robust warmup. This prevents one delayed timestamp from
    // becoming the baseline for the next adaptive epoch while still reacting within a few frames.
    std::size_t nrWarmupSamples = 3;
    double queueHigh = 0.78;
    double queueLow = 0.30;
    double frameOverBudgetRatio = 1.03;
    // A frame overrun alone cannot justify quality loss. NR must account for at least this fraction
    // of its budget before frame-time pressure is allowed to trigger a scale reduction.
    double frameHeadroomRatio = 0.82;
    // Frame pressure may lower neural resolution only when NR is a material part of the frame.
    double frameNrMaterialRatio = 0.75;
    double nrOverBudgetRatio = 1.08;
    double nrHeadroomRatio = 0.78;
    double processedRatioLow = 0.90;
    double processedRatioGood = 0.98;
    double sourceCapSafety = 0.97;
    double minSourceCapFps = 30.0;
    double governorRecoveryFpsPerSecond = 8.0;
    double governorReleaseHeadroom = 1.03;

    // Model cost is approximately area-scaled. Predictive downshift uses sqrt(target/current)
    // and snaps to a discrete rung, reducing repeated rebuilds during a sudden load increase.
    bool predictiveScaleDown = true;
    double predictiveTargetUtilization = 0.92;
    std::size_t maxPredictiveStepDrop = 2;

    std::vector<float> scaleSteps {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f};
    float minScale = 0.50f;
    float maxScale = 1.00f;

    bool operator==(const PerformanceConfig&) const = default;
};

struct PerformanceDecision {
    float workingScale = 1.0f;
    bool changedScale = false;
    // Set once per over-budget episode when a cheaper precision exists: the controller held the
    // resolution and is asking for that trade instead. Dropping WorkingScale costs detail
    // everywhere; dropping precision costs fidelity in the neural pass alone.
    bool wantsPrecisionRelief = false;
    bool overBudget = false;
    bool hasHeadroom = false;
    bool telemetryReady = false;
    bool gpuFrameTimingAvailable = false;
    AdaptiveState state = AdaptiveState::WaitingForTiming;
    double recommendedSourceCapFps = 0.0; // 0 = uncapped/no recommendation
    double effectiveNrCriticalMs = 0.0;
    double resolvedNrBudgetMs = 0.0;
};

class PerformanceController {
public:
    explicit PerformanceController(PerformanceConfig config = {});

    void Reset(float initialScale = 1.0f);
    PerformanceDecision Update(const TelemetrySample& sample);
    void ObserveScaleCost(float scale, double gpuMs);
    void ClearLearnedCostModel();
    std::optional<NrCostFit> LearnedCostFit() const;

    // Quarantine a work-resolution rung that failed to build on the current host/device. Reduced
    // scales prefer falling back upward because full/native is the compatibility baseline.
    std::optional<float> ReportScaleBuildFailure(float failedScale);
    void ClearScaleBuildFailures();
    bool IsScaleBuildFailed(float scale) const;

    // Tell the controller whether a cheaper precision is available right now. While it is, the
    // first downshift of each over-budget episode is spent on precision instead of resolution; if
    // the pass is still over budget afterwards, the next one moves the scale as before.
    void SetPrecisionReliefAvailable(bool available) noexcept;

    float WorkingScale() const noexcept;
    const PerformanceConfig& Config() const noexcept;

private:
    static double Ewma(double previous, double value, double alpha);
    static double Median(const std::vector<double>& values);
    std::size_t FindNearestStep(float value) const;
    bool CanScaleDown() const;
    bool CanScaleUp() const;
    void MoveDown();
    void MoveDownToward(float desiredScale);
    void MoveUp();
    double ResolveNrBudgetMs(double frameBudgetMs) const;

    PerformanceConfig config_;
    std::size_t scaleIndex_ = 0;
    double overBudgetSeconds_ = 0.0;
    double headroomSeconds_ = 0.0;
    double cooldownRemaining_ = 0.0;

    bool initialized_ = false;
    bool awaitingNrTiming_ = true;
    bool precisionReliefAvailable_ = false;
    bool precisionReliefSpent_ = false;
    std::vector<double> nrWarmupSamples_;
    std::vector<double> frameRecentSamples_;
    // Keep a short raw window for the control signal. A median over this window makes one
    // scheduler/clock stall invisible to the scale governor while three consecutive high samples
    // still become a real sustained overload.
    std::vector<double> nrRecentSamples_;
    double emaNrMs_ = 0.0;
    double emaFrameMs_ = 0.0;
    double emaQueuePressure_ = 0.0;
    double emaSourceFps_ = 0.0;
    double emaProcessedFps_ = 0.0;
    double emaAsyncOverlap_ = 0.0;
    double governorCapFps_ = 0.0;
    std::vector<bool> failedScaleSteps_;
    NrCostModel costModel_{config_.scaleSteps.size()};
};

} // namespace nrfusion
