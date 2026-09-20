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
    float fixedScale = 1.0f; // authoritative only when enabled=false / Custom
    double targetFps = 120.0;
};

class OptiScalerAdapter {
public:
    static OptiScalerAdapter& Instance();

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

    // Precisions the host last reported this device as having. A menu offers only these: a format
    // the hardware lacks is a dead control, not a slower option. Empty until the first decision,
    // because before that the adapter has been told nothing about the device.
    std::vector<NrPrecision> SupportedPrecisions() const;
    FusionRuntime& FusionRuntimeEngine() noexcept;
    const FusionRuntime& FusionRuntimeEngine() const noexcept;
    std::string DiagnosticsText() const;

    // Exact neural-work telemetry. These methods are safe to call from the host around the serial
    // timer today and from future async/MGPU executors without changing workload identity.
    WorkTicket BeginNrWork(std::uint64_t sourceFrame, std::uint64_t viewKey = 0,
                           float workingScale = 1.0f, std::uint8_t precisionTag = 0);
    bool SubmitNrWork(const WorkTicket& ticket);
    bool AbandonNrWork(const WorkTicket& ticket);
    bool CompleteNrWork(const WorkTicket& ticket);
    // Associates an explicit same-workload visual/temporal verdict with a submitted ticket. The
    // timing-first Auto path does not require a verdict; an explicit rejected verdict can still
    // veto a candidate, and strict hosts can require one for every sample.
    bool ReportQualityEvidence(const WorkTicket& ticket, const QualityEvidence& quality);

    // Bounded executor contract used by future Async/NVOF/MGPU/x86 backends. Admission submits the
    // WorkId exactly once; completion updates throughput when GPU/helper work is actually ready.
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
    // Returns each newly retired NR timestamp once. A return value of zero means no fresh GPU
    // timing is available for this decision; callers must not replay the previous value.
    double ConsumeTrackedNrGpuMs();

    NrPrecision ResolvePrecision(bool automaticEnabled, bool candidateAvailable,
                                 NrPrecision manualPrecision = NrPrecision::Fp8);
    void ReportPrecisionCandidateFailure();
    PrecisionAutotuneResult PrecisionStatus() const;
    bool PrecisionCandidateRequested() const;

    // Cross-queue timing path used by the real D3D12 async executor: calibration maps independent
    // queue timestamp domains to QPC, overlap is measured there, and the hidden qualifier decides
    // whether async is worth keeping for the current shape.
    bool UpdateQueueClock(QueueClockId queue, const QueueClockCalibrationSample& sample);
    template <typename QueueT>
    bool UpdateD3D12QueueClock(QueueT* queue, double cpuQpcFrequencyHz) {
        QueueClockCalibrationSample sample;
        if (!SampleD3D12QueueClock(queue, cpuQpcFrequencyHz, sample)) return false;
        return UpdateQueueClock(D3D12QueueClockId(queue), sample);
    }
    // sampleId must identify the same source-frame/work sample passed to ObserveAsyncTrial.
    // Async qualification rejects overlap measured for any older/newer sample.
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
    OptiScalerAdapter();
    void ReconfigureIfNeeded(const AdaptiveSettings& settings);
    static bool SettingsEquivalent(const AdaptiveSettings& a, const AdaptiveSettings& b);
    static PerformanceConfig ToConfig(const AdaptiveSettings& settings);

    mutable std::mutex mutex_;
    AdaptiveSettings settings_{};
    PerformanceDecision lastDecision_{};
    TelemetrySample lastTelemetrySample_{};
    std::chrono::steady_clock::time_point lastUpdate_{};
    bool haveTimestamp_ = false;
    // Resource incompatibility belongs to the host/session, not to a UI preset. Keep quarantined
    // working scales across Target-FPS/mode controller rebuilds until the host shape/session resets.
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
    // Precision cost is workload-size dependent. Cache the qualified result per WorkingScale
    // for the current base shape so dynamic scaling can revisit a rung without another feature rebuild.
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

} // namespace nrfusion
