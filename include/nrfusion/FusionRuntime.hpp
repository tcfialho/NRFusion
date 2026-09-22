#pragma once
#include "nrfusion/AutoDecision.hpp"
#include "nrfusion/AutoTuneCoordinator.hpp"
#include "nrfusion/CompatibilityDatabase.hpp"
#include "nrfusion/AsyncOverlapEstimator.hpp"
#include "nrfusion/AsyncQualification.hpp"
#include "nrfusion/CrossQueueClockCalibrator.hpp"
#include "nrfusion/FrameLimitPolicy.hpp"
#include "nrfusion/GuideValidation.hpp"
#include "nrfusion/MgpuPlanner.hpp"
#include "nrfusion/MotionConfidence.hpp"
#include "nrfusion/NvofPolicy.hpp"
#include "nrfusion/PerformanceController.hpp"
#include "nrfusion/PipelinedExecutorState.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/ResidualEngine.hpp"
#include "nrfusion/ResidualPolicy.hpp"
#include "nrfusion/ResidualReprojection.hpp"
#include "nrfusion/PipelinePolicy.hpp"
#include "nrfusion/SchedulerPolicy.hpp"
#include "nrfusion/TelemetryTracker.hpp"
#include "nrfusion/TemporalConfidence.hpp"
#include "nrfusion/TemporalHistoryRegistry.hpp"
#include "nrfusion/TimingWorkMapper.hpp"
#include "nrfusion/WorkLedger.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace nrfusion {

// Portable orchestration object owned by a host runtime. Hardware executors stay outside this class;
// their measured events and capabilities are fed in through these policies.
class FusionRuntime {
public:
    explicit FusionRuntime(PerformanceConfig config = {}) : performance_(std::move(config)) {}

    PerformanceDecision OnTelemetry(const TelemetrySample& sample) { return performance_.Update(sample); }
    // Changes whenever the central Auto path starts measuring a different structural or execution
    // configuration. Adapters use this epoch to reject late work from the previous configuration.
    std::uint64_t AutoConfigurationGeneration() const noexcept { return autoPrecisionGeneration_; }
    PipelineDecision ResolvePipeline(const GameContext& game, const FrameContext& frame,
                                     const RuntimeCapabilities& capabilities,
                                     const CompatibilityOverride* compatibility = nullptr) const {
        const RuntimeCapabilities effective = compatibility
            ? CompatibilityDatabase::ConstrainCapabilities(capabilities, *compatibility) : capabilities;
        const std::optional<FrameProvider> preferred = compatibility
            ? compatibility->preferredProvider : std::nullopt;
        return pipelinePolicy_.Choose(game, frame, effective, preferred);
    }

    // Central Auto path. Telemetry is interpreted as belonging to the previously emitted Auto
    // configuration; when provider/shape/scheduler/precision changes, adaptive state is reset before
    // measurements from the new workload are allowed to influence WorkingScale.
    AutoDecision ResolveAuto(const GameContext& game, const FrameContext& frame,
                             const TelemetrySample& sample, const RuntimeCapabilities& capabilities,
                             SchedulerMode requested = SchedulerMode::Auto,
                             unsigned generationMultiplier = 1,
                             const CompatibilityOverride* compatibility = nullptr) {
        const RuntimeCapabilities effectiveCaps = compatibility
            ? CompatibilityDatabase::ConstrainCapabilities(capabilities, *compatibility) : capabilities;
        const std::optional<FrameProvider> preferredProvider = compatibility
            ? compatibility->preferredProvider : std::nullopt;
        AutoDecision out;
        out.pipeline = pipelinePolicy_.Choose(game, frame, effectiveCaps, preferredProvider);
        const bool apiConsistent = game.api != GraphicsApi::Unknown && frame.api == game.api;
        const bool frameContractReady = frame.ReadyForCore() && apiConsistent;
        out.supported = out.pipeline.supported && effectiveCaps.fp8 && frameContractReady;
        if (!out.supported) {
            if (haveAutoDecision_) ResetAutoAdaptiveState(performance_.WorkingScale());
            haveAutoDecision_ = false;
            haveAutoStructure_ = false;
            out.workingScale = performance_.WorkingScale();
            out.performance.workingScale = out.workingScale;
            return out;
        }

        const Resolution renderSize = frame.RenderSize();
        const Resolution outputSize = frame.OutputSize();
        const AutoStructuralIdentity structure{out.pipeline.provider, out.pipeline.transport,
                                               out.pipeline.api, out.pipeline.placement,
                                               out.pipeline.motion, renderSize, outputSize};
        const bool structuralChanged = !haveAutoStructure_ || !(structure == autoStructure_);
        if (structuralChanged) {
            ResetAutoAdaptiveState(performance_.WorkingScale());
            autoStructure_ = structure;
            haveAutoStructure_ = true;
        }

        TelemetrySample constrained = sample;
        constrained.asyncComputeAvailable = constrained.asyncComputeAvailable && effectiveCaps.asyncCompute;
        constrained.secondaryGpuAvailable = constrained.secondaryGpuAvailable && effectiveCaps.secondaryGpu;
        if (structuralChanged) {
            // Availability can survive a resize, but old overlap/deadline stability cannot be assumed
            // to describe the new workload. Auto re-qualifies conservatively from Serialized.
            constrained.asyncComputeStable = false;
            constrained.secondaryGpuStable = false;
            constrained.asyncOverlap = 0.0;
        }
        out.scheduler = scheduler_.Choose(constrained, requested);

        PerformanceDecision perf;
        if (!haveAutoDecision_ || structuralChanged) {
            // No sample from the previous execution configuration belongs to this new workload.
            // Do not feed a synthetic zero sample into the controller: that would initialize its
            // EMAs at zero and underweight the first real frame/queue measurement after a resize,
            // provider switch, or first Auto activation. Preserve the current rung until a real
            // sample from this exact configuration arrives on the next decision.
            perf.workingScale = performance_.WorkingScale();
        } else {
            TelemetrySample controllerSample = constrained;
            NormalizeTelemetryForScheduler(controllerSample, autoScheduler_);
            // Offer the precision trade only while a cheaper format exists on this GPU and the run
            // is not already on it. On hardware with a single supported format there is nothing to
            // trade and the controller keeps moving resolution, exactly as before.
            performance_.SetPrecisionReliefAvailable(
                CheaperPrecision(autoPrecision_, effectiveCaps.fp8, effectiveCaps.hybridNvfp4)
                    .has_value());
            perf = performance_.Update(controllerSample);
        }

        const bool precisionConfigChanged = !haveAutoPrecisionConfig_ ||
            std::fabs(autoPrecisionScale_ - perf.workingScale) > 1e-4f ||
            autoPrecisionProvider_ != out.pipeline.provider || autoPrecisionApi_ != out.pipeline.api ||
            autoPrecisionPlacement_ != out.pipeline.placement || autoPrecisionScheduler_ != out.scheduler ||
            autoPrecisionRenderSize_ != renderSize || autoPrecisionOutputSize_ != outputSize;
        if (precisionConfigChanged) {
            autoPrecisionGeneration_ = NextAutoConfigurationGeneration(
                autoPrecisionGeneration_, "Auto precision generation");
            precisionTuner_.Reset(autoPrecisionGeneration_);
            autoPrecisionScale_ = perf.workingScale;
            autoPrecisionProvider_ = out.pipeline.provider;
            autoPrecisionApi_ = out.pipeline.api;
            autoPrecisionPlacement_ = out.pipeline.placement;
            autoPrecisionScheduler_ = out.scheduler;
            autoPrecisionRenderSize_ = renderSize;
            autoPrecisionOutputSize_ = outputSize;
            haveAutoPrecisionConfig_ = true;
        } else if (!effectiveCaps.hybridNvfp4) {
            precisionTuner_.ReportCandidateFailure(precisionTuner_.ConfigurationGeneration());
        }

        out.precision = precisionTuner_.Desired(true, effectiveCaps.hybridNvfp4);
        if (perf.wantsPrecisionRelief) {
            // The controller held the resolution and asked for this instead. Honour it only with a
            // format this GPU really has; otherwise the request silently expires and the next
            // decision moves the scale.
            if (const auto cheaper = CheaperPrecision(out.precision, effectiveCaps.fp8,
                                                      effectiveCaps.hybridNvfp4))
                out.precision = *cheaper;
        }
        out.workingScale = perf.workingScale;
        out.sourceCapFps = perf.recommendedSourceCapFps;
        out.performance = perf;

        if (game.frameGeneration && sample.fgEnabled && effectiveCaps.frameGeneration &&
            effectiveCaps.maxGenerationMultiplier >= 2) {
            const unsigned maxMultiplier = std::min<unsigned>(effectiveCaps.maxGenerationMultiplier, 4u);
            out.generationMultiplier = std::clamp(generationMultiplier, 2u, maxMultiplier);
            out.presentation = out.generationMultiplier > 2 ? PresentationMode::MultiFrameGeneration
                                                             : PresentationMode::FrameGeneration;
        }

        const bool executionChanged = structuralChanged || !haveAutoDecision_ ||
                                      autoScheduler_ != out.scheduler || autoPrecision_ != out.precision;
        if (haveAutoDecision_ && executionChanged) {
            // The decision above is still valid for the next frame, but its measurements must start a
            // fresh adaptive epoch. Preserve the selected rung while discarding old EMA/cost history.
            performance_.ClearLearnedCostModel();
            performance_.Reset(out.workingScale);
        }
        autoScheduler_ = out.scheduler;
        autoPrecision_ = out.precision;
        haveAutoDecision_ = true;
        return out;
    }
    SchedulerMode ResolveScheduler(const TelemetrySample& sample, SchedulerMode requested = SchedulerMode::Auto) const {
        return scheduler_.Choose(sample, requested);
    }
    ResidualDecision ResolveResidual(const ResidualInputs& input) const { return residual_.Decide(input); }
    TemporalConfidenceResult ResolveTemporalConfidence(const TemporalConfidenceInput& input) const { return temporal_.Evaluate(input); }
    MgpuPlan ResolveMgpu(const MgpuInput& input) const { return mgpu_.Plan(input); }
    NvofResolutionPlan ResolveNvof(std::uint32_t width, std::uint32_t height,
                                   NvofResolution requested = NvofResolution::Auto) const {
        return nvof_.Resolve(width, height, requested);
    }
    MotionGuideResult UpdateMotionGuide(const MotionGuideSample& sample) { return motionValidator_.Update(sample); }
    MotionConfidenceResult ResolveMotionConfidence(const MotionConfidenceInput& input) const {
        return motionConfidence_.Evaluate(input);
    }
    HistoryLease AcquireHistory(const ViewDescriptor& view, std::uint64_t frameNumber) {
        return histories_.Acquire(view, frameNumber);
    }
    void InvalidateHistory(std::uint64_t featureKey) { histories_.InvalidateFeature(featureKey); }

    TelemetryTracker& Telemetry() noexcept { return telemetry_; }
    const TelemetryTracker& Telemetry() const noexcept { return telemetry_; }
    PipelinedExecutorState& Pipeline() noexcept { return pipeline_; }
    const PipelinedExecutorState& Pipeline() const noexcept { return pipeline_; }
    WorkLedger& Works() noexcept { return works_; }
    TimingWorkMapper& TimingMap() noexcept { return timingMap_; }
    AsyncOverlapEstimator& Overlap() noexcept { return overlap_; }

    std::optional<double> ObserveCalibratedOverlap(
        const QueueGpuIntervalTicks& nr,
        const std::vector<QueueGpuIntervalTicks>& concurrent,
        double dtSeconds);
    CrossQueueClockCalibrator& QueueClocks() noexcept { return queueClocks_; }
    const CrossQueueClockCalibrator& QueueClocks() const noexcept { return queueClocks_; }
    AsyncQualification& AsyncTuner() noexcept { return asyncTuner_; }
    const AsyncQualification& AsyncTuner() const noexcept { return asyncTuner_; }
    AutoTuneCoordinator& AutoTune() noexcept { return autoTune_; }
    const AutoTuneCoordinator& AutoTune() const noexcept { return autoTune_; }
    PrecisionAutotuner& PrecisionTuner() noexcept { return precisionTuner_; }
    const PrecisionAutotuner& PrecisionTuner() const noexcept { return precisionTuner_; }
    ResidualReprojection& ResidualTransport() noexcept { return residualTransport_; }
    ResidualEngine& Residuals() noexcept { return residualEngine_; }
    const ResidualEngine& Residuals() const noexcept { return residualEngine_; }
    float WorkingScale() const noexcept { return performance_.WorkingScale(); }

    // Encaminhamentos que o trabalho de 12/09 acrescentou: o adaptador passou a falar
    // com o runtime central em vez de manter um PerformanceController proprio.
    void Reconfigure(const PerformanceConfig& config) { performance_ = PerformanceController(config); }
    void Reset(float initialScale) { performance_.Reset(initialScale); }
    void ObserveScaleCost(float scale, double gpuMs) { performance_.ObserveScaleCost(scale, gpuMs); }
    void ClearLearnedCostModel() { performance_.ClearLearnedCostModel(); }
    std::optional<float> ReportScaleBuildFailure(float failedScale) {
        return performance_.ReportScaleBuildFailure(failedScale);
    }
    void ClearScaleBuildFailures() { performance_.ClearScaleBuildFailures(); }
    const PerformanceConfig& PerformanceCfg() const noexcept { return performance_.Config(); }
    void BeginConfigurationEpoch(float initialScale) {
        autoPrecisionGeneration_ = NextAutoConfigurationGeneration(
            autoPrecisionGeneration_, "Auto precision generation");
        ResetAutoAdaptiveState(initialScale);
        haveAutoDecision_ = false;
    }

private:
    void ResetAutoAdaptiveState(float initialScale) {
        performance_.ClearLearnedCostModel();
        performance_.Reset(initialScale);
        asyncTuner_.Reset();
        overlap_.Reset();
        autoTune_.Reset();
        haveAutoStructure_ = false;
        haveAutoPrecisionConfig_ = false;
        precisionTuner_.Reset(autoPrecisionGeneration_);
    }

    PerformanceController performance_;
    SchedulerPolicy scheduler_;
    PipelinePolicy pipelinePolicy_;
    ResidualPolicy residual_;
    TemporalConfidence temporal_;
    MgpuPlanner mgpu_;
    NvofPolicy nvof_;
    MotionGuideValidator motionValidator_;
    MotionConfidenceEngine motionConfidence_;
    TemporalHistoryRegistry histories_;
    TelemetryTracker telemetry_;
    PipelinedExecutorState pipeline_{2};
    WorkLedger works_;
    TimingWorkMapper timingMap_{16};
    AsyncOverlapEstimator overlap_;
    CrossQueueClockCalibrator queueClocks_;
    AsyncQualification asyncTuner_;
    PrecisionAutotuner precisionTuner_;
    AutoTuneCoordinator autoTune_;
    bool haveAutoPrecisionConfig_ = false;
    std::uint64_t autoPrecisionGeneration_ = 1;
    float autoPrecisionScale_ = 1.0f;
    FrameProvider autoPrecisionProvider_ = FrameProvider::Unsupported;
    GraphicsApi autoPrecisionApi_ = GraphicsApi::Unknown;
    NrPlacement autoPrecisionPlacement_ = NrPlacement::Auto;
    SchedulerMode autoPrecisionScheduler_ = SchedulerMode::Serialized;
    Resolution autoPrecisionRenderSize_{};
    Resolution autoPrecisionOutputSize_{};
    bool haveAutoStructure_ = false;
    bool haveAutoDecision_ = false;
    AutoStructuralIdentity autoStructure_{};
    SchedulerMode autoScheduler_ = SchedulerMode::Serialized;
    NrPrecision autoPrecision_ = NrPrecision::Fp8;
    ResidualReprojection residualTransport_;
    ResidualEngine residualEngine_;
};

} // namespace nrfusion
