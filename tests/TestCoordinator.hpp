#pragma once

#include "nrfusion/AutoDecision.hpp"
#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/AsyncOverlapEstimator.hpp"
#include "nrfusion/AsyncQualification.hpp"
#include "nrfusion/CrossQueueClockCalibrator.hpp"
#include "nrfusion/D3D12QueueClockBridge.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/PipelinedExecutorState.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/TelemetryTracker.hpp"
#include "nrfusion/TimingWorkMapper.hpp"
#include "nrfusion/WorkLedger.hpp"
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nrfusion {

struct AdaptiveSettings {
    bool enabled = true;
    UserMode mode = UserMode::Auto;
    float fixedScale = 1.0f;
    double targetFps = 120.0;
};

class TestCoordinator {
public:
    static TestCoordinator& Instance();

    float ResolveWorkingScale(const AdaptiveSettings& settings,
                              double nrGpuMs,
                              double frameGpuMs = 0.0,
                              double sourceFps = 0.0,
                              double processedFps = 0.0,
                              double queuePressure = 0.0,
                              double asyncOverlap = 0.0,
                              FrameTimingSource frameTimingSource = FrameTimingSource::GpuTimestamp);

    AutoDecision ResolveAuto(const GameContext& game, const FrameContext& frame,
                             const RuntimeCapabilities& capabilities,
                             const AdaptiveSettings& settings,
                             double nrGpuMs,
                             double frameGpuMs = 0.0,
                             double sourceFps = 0.0,
                             double processedFps = 0.0,
                             double queuePressure = 0.0,
                             double asyncOverlap = 0.0,
                             SchedulerMode requestedScheduler = SchedulerMode::Auto,
                             unsigned generationMultiplier = 1,
                             const CompatibilityOverride* compatibility = nullptr,
                             FrameTimingSource frameTimingSource = FrameTimingSource::GpuTimestamp);

    PerformanceDecision LastDecision() const;
    AutoDecision LastAutoDecision() const;

    std::vector<NrPrecision> SupportedPrecisions() const;
    FusionRuntime& FusionRuntimeEngine() noexcept;
    const FusionRuntime& FusionRuntimeEngine() const noexcept;
    std::string DiagnosticsText() const;

    WorkTicket BeginNrWork(std::uint64_t sourceFrame, std::uint64_t viewKey = 0,
                           float workingScale = 1.0f, std::uint8_t precisionTag = 0);
    bool SubmitNrWork(const WorkTicket& ticket);
    bool AbandonNrWork(const WorkTicket& ticket);
    bool CompleteNrWork(const WorkTicket& ticket);
    bool ReportQualityEvidence(const WorkTicket& ticket, const QualityEvidence& quality);

    std::optional<PipelineTicket> AdmitPipelinedWork(const WorkTicket& ticket);
    bool CompletePipelinedWork(const PipelineTicket& pipelineTicket, const WorkTicket& workTicket);
    bool AbandonPipelinedWork(const PipelineTicket& pipelineTicket, const WorkTicket& workTicket);
    std::optional<PipelineReadyFrame> ConsumePipelinedBefore(std::uint64_t currentFrame, std::uint64_t viewKey = 0);
    void RequestPipelineReconfigure();
    std::size_t DiscardPipelineReadyForReconfigure();
    bool ApplyPipelineReconfigureIfIdle();
    double PipelineQueuePressure() const;
    bool PipelineReconfigurePending() const;

    bool MapTimedWork(const WorkTicket& ticket);
    void MapInvalidTimedAttempt();
    bool RetireTimedInterval(double gpuMs);
    bool MapTimedSlot(std::size_t slot, const WorkTicket& ticket);
    bool RetireTimedSlot(std::size_t slot, double gpuMs);
    void ClearTimedSlot(std::size_t slot);
    double TrackedSourceFps() const;
    double TrackedProcessedFps() const;
    double TrackedNrGpuMs() const;
    double ConsumeTrackedNrGpuMs();

    NrPrecision ResolvePrecision(bool automaticEnabled, bool candidateAvailable,
                                 NrPrecision manualPrecision = NrPrecision::Fp8);
    void ReportPrecisionCandidateFailure();
    PrecisionAutotuneResult PrecisionStatus() const;
    bool PrecisionCandidateRequested() const;

    bool UpdateQueueClock(QueueClockId queue, const QueueClockCalibrationSample& sample);
    template <typename QueueT>
    bool UpdateD3D12QueueClock(QueueT* queue, double cpuQpcFrequencyHz) {
        QueueClockCalibrationSample sample;
        if (!SampleD3D12QueueClock(queue, cpuQpcFrequencyHz, sample)) return false;
        return UpdateQueueClock(D3D12QueueClockId(queue), sample);
    }
    std::optional<double> ObserveAsyncOverlap(std::uint64_t sampleId, const QueueGpuIntervalTicks& nr,
                                               const std::vector<QueueGpuIntervalTicks>& concurrent,
                                               double dtSeconds);
    void ObserveAsyncTrial(std::uint64_t sampleId, SchedulerMode mode, double frameGpuMs,
                           double queuePressure,
                           FrameTimingSource frameTimingSource = FrameTimingSource::GpuTimestamp);
    SchedulerMode QualifiedScheduler(bool asyncAvailable) const;
    AsyncQualificationResult AsyncStatus() const;
    QueueClockCalibrationStatus QueueClockStatus(QueueClockId queue) const;

    std::uint64_t ScaleGeneration() const;
    std::optional<float> ReportWorkingScaleBuildFailure(float failedScale);
    void ClearWorkingScaleBuildFailures();
    void Reset(float initialScale = 1.0f);
    void ResetForShape(const AdaptiveSettings& settings);

private:
    TestCoordinator();
    void ReconfigureIfNeeded(const AdaptiveSettings& settings);
    static bool SettingsEquivalent(const AdaptiveSettings& a, const AdaptiveSettings& b);
    static PerformanceConfig ToConfig(const AdaptiveSettings& settings);

    mutable std::mutex mutex_;
    AdaptiveSettings settings_{};
    PerformanceDecision lastDecision_{};
    TelemetrySample lastTelemetrySample_{};
    std::chrono::steady_clock::time_point lastUpdate_{};
    bool haveTimestamp_ = false;
    std::vector<float> failedWorkingScales_;
    WorkLedger works_;
    TimingWorkMapper timingMap_{16};
    TimingSlotMapper timingSlots_{8};
    TelemetryTracker telemetry_{{0.50, 0.75, 16}};
    PipelinedExecutorState pipeline_{3};
    std::optional<float> compatibilityOverride_;
    std::uint64_t scaleGeneration_ = 1;
    double matchedNrGpuMs_ = 0.0;
    bool haveMatchedNrGpuMs_ = false;
    std::uint64_t matchedNrTimingSequence_ = 0;
    std::uint64_t consumedNrTimingSequence_ = 0;
    PrecisionAutotuner precisionTuner_;
    CrossQueueClockCalibrator queueClocks_;
    AsyncOverlapEstimator asyncOverlap_;
    std::vector<GpuInterval> asyncCommonIntervals_;
    AsyncQualification asyncTuner_;
    std::uint64_t freshAsyncOverlapSampleId_ = 0;
    std::unordered_map<int, bool> precisionByScale_;
    std::unordered_map<std::uint64_t, QualityEvidence> qualityEvidenceByWork_;

    static int PrecisionScaleKey(float workingScale);
    void CacheFinishedPrecisionLocked(float workingScale);
    void AdvanceScaleGenerationLocked();
    void AcceptTimingLocked(const WorkTicket& ticket, double gpuMs);

    AutoDecision lastAutoDecision_{};
    bool lastCapsFp8_ = false;
    bool lastCapsHybridNvfp4_ = false;
    FusionRuntime runtime_{};
};

using OptiScalerAdapter = TestCoordinator;

} // namespace nrfusion
