#include "nrfusion/OptiScalerAdapter.hpp"
#include "nrfusion/Presets.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>

namespace nrfusion {
namespace {
double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

float SanitizeScale(float value) {
    if (!std::isfinite(value)) value = 1.0f;
    return std::clamp(value, 0.25f, 2.0f);
}

double SanitizeTargetFps(double value) {
    if (!std::isfinite(value)) value = 120.0;
    return std::clamp(value, 30.0, 1000.0);
}

const char* FrameTimingSourceName(FrameTimingSource source) noexcept {
    switch (source) {
    case FrameTimingSource::GpuTimestamp: return "gpu-timestamp";
    case FrameTimingSource::PresentationInterval: return "presentation-interval";
    default: return "unknown";
    }
}

const char* UserModeName(UserMode mode) noexcept {
    switch (mode) {
    case UserMode::Auto: return "auto";
    case UserMode::MaxFps: return "max-fps";
    case UserMode::Balanced: return "balanced";
    case UserMode::Quality: return "best-quality";
    case UserMode::Custom: return "custom";
    default: return "unknown";
    }
}

const char* SchedulerModeName(SchedulerMode mode) noexcept {
    switch (mode) {
    case SchedulerMode::Serialized: return "serialized";
    case SchedulerMode::AsyncCompute: return "async";
    case SchedulerMode::SecondaryGpu: return "secondary-gpu";
    default: return "auto";
    }
}

const char* PrecisionName(NrPrecision precision) noexcept {
    return precision == NrPrecision::HybridNvfp4 ? "hybrid-nvfp4" : "fp8";
}

const char* AdaptiveStateName(AdaptiveState state) noexcept {
    switch (state) {
    case AdaptiveState::WaitingForTiming: return "waiting-for-timing";
    case AdaptiveState::Stable: return "stable";
    case AdaptiveState::ReducingScale: return "reducing-scale";
    case AdaptiveState::ExternalPressure: return "external-pressure";
    case AdaptiveState::RecoveringQuality: return "recovering-quality";
    default: return "unknown";
    }
}

AdaptiveSettings NormalizeSettings(AdaptiveSettings settings) {
    settings.fixedScale = SanitizeScale(settings.fixedScale);
    settings.targetFps = SanitizeTargetFps(settings.targetFps);
    if (settings.mode == UserMode::Custom) settings.enabled = false;
    return settings;
}
}

PerformanceConfig OptiScalerAdapter::ToConfig(const AdaptiveSettings& s) {
    PerformancePreset preset = PerformancePreset::Auto;
    switch (s.mode) {
    case UserMode::MaxFps: preset = PerformancePreset::Performance; break;
    case UserMode::Balanced: preset = PerformancePreset::Balanced; break;
    case UserMode::Quality: preset = PerformancePreset::Quality; break;
    case UserMode::Auto: preset = PerformancePreset::Auto; break;
    case UserMode::Custom:
        // Custom disables adaptation in ResolveWorkingScale. Keep a harmless controller
        // configuration only so mode transitions retain a valid object.
        return MakePerformanceConfig(PerformancePreset::Auto, SanitizeTargetFps(s.targetFps));
    }
    return MakePerformanceConfig(preset, SanitizeTargetFps(s.targetFps));
}

OptiScalerAdapter& OptiScalerAdapter::Instance() {
    static OptiScalerAdapter instance;
    return instance;
}

OptiScalerAdapter::OptiScalerAdapter()
    : runtime_(ToConfig(settings_)) {
    precisionTuner_.Reset(scaleGeneration_);
}

int OptiScalerAdapter::PrecisionScaleKey(float workingScale) {
    if (!std::isfinite(workingScale)) workingScale = 1.0f;
    return static_cast<int>(std::lround(std::clamp(workingScale, 0.25f, 2.0f) * 1000.0f));
}

void OptiScalerAdapter::CacheFinishedPrecisionLocked(float workingScale) {
    const auto result = precisionTuner_.Result();
    if (!result.finished) return;
    precisionByScale_[PrecisionScaleKey(workingScale)] = result.candidateQualified;
}

bool OptiScalerAdapter::SettingsEquivalent(const AdaptiveSettings& a, const AdaptiveSettings& b) {
    auto near = [](double x, double y) { return std::fabs(x - y) < 1e-5; };
    return a.enabled == b.enabled && a.mode == b.mode &&
           near(static_cast<double>(a.fixedScale), static_cast<double>(b.fixedScale)) &&
           near(a.targetFps, b.targetFps);
}

void OptiScalerAdapter::AdvanceScaleGenerationLocked() {
    if (scaleGeneration_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("NRFusion scale generation namespace exhausted");
    ++scaleGeneration_;
    matchedNrGpuMs_ = 0.0;
    haveMatchedNrGpuMs_ = false;
    precisionTuner_.Reset(scaleGeneration_);
}

void OptiScalerAdapter::AcceptTimingLocked(const WorkTicket& ticket, double gpuMs) {
    QualityEvidence quality{};
    if (const auto qualityIt = qualityEvidenceByWork_.find(ticket.id);
        qualityIt != qualityEvidenceByWork_.end()) {
        quality = qualityIt->second;
        qualityEvidenceByWork_.erase(qualityIt);
    }
    if (ticket.configurationGeneration != scaleGeneration_) return;
    if (!std::isfinite(gpuMs) || gpuMs <= 0.0 || gpuMs >= 1000.0) return;
    matchedNrGpuMs_ = gpuMs;
    haveMatchedNrGpuMs_ = true;
    if (matchedNrTimingSequence_ == std::numeric_limits<std::uint64_t>::max()) {
        // The sequence namespace is exhausted; discard the sample rather than allowing a wrapped
        // value to be mistaken for a fresh timing.
        matchedNrGpuMs_ = 0.0;
        haveMatchedNrGpuMs_ = false;
        return;
    }
    ++matchedNrTimingSequence_;
    runtime_.ObserveScaleCost(ticket.workingScale, gpuMs);
    precisionTuner_.Observe(ticket.precisionTag == 4 ? NrPrecision::HybridNvfp4 : NrPrecision::Fp8,
                            ticket.configurationGeneration, gpuMs, quality);
    CacheFinishedPrecisionLocked(ticket.workingScale);
}

void OptiScalerAdapter::ReconfigureIfNeeded(const AdaptiveSettings& s) {
    if (SettingsEquivalent(settings_, s)) return;
    const float previous = runtime_.WorkingScale();
    const double previousTargetFps = runtime_.PerformanceCfg().targetFps;
    const UserMode previousMode = settings_.mode;
    settings_ = s;
    const auto config = ToConfig(settings_);
    runtime_.Reconfigure(config);
    // Async baseline/trial frame times are only comparable under the same pacing target/mode.
    // A user-facing mode or Target-FPS change restarts that hidden qualification, while queue clock
    // calibration itself remains valid for the device/queue pair.
    asyncOverlap_.Reset();
    asyncTuner_.Reset();
    freshAsyncOverlapSampleId_ = 0;
    for (float failedScale : failedWorkingScales_) {
        runtime_.ReportScaleBuildFailure(failedScale);
    }
    // Um alvo de FPS diferente e um orcamento diferente, e o degrau atual foi escolhido para o
    // orcamento antigo. Herda-lo congela a decisao: a epoca reinicia, o controlador volta a
    // esperar por tempo fresco, e o degrau herdado fica ate que uma violacao sustentada apareca.
    // Recomecar do teto faz o controlador derivar de novo a partir do orcamento que vale agora.
    const bool sameContract = std::fabs(static_cast<double>(config.targetFps) -
                                        static_cast<double>(previousTargetFps)) < 1e-6 &&
                              previousMode == settings_.mode;
    runtime_.Reset(sameContract ? std::clamp(previous, config.minScale, config.maxScale)
                                : config.maxScale);
    // Mode/Target-FPS changes define a new configuration epoch even if the selected scale is
    // unchanged. Late timings from the old controller must never train the new cost/precision state.
    AdvanceScaleGenerationLocked();
    haveTimestamp_ = false;
}

float OptiScalerAdapter::ResolveWorkingScale(const AdaptiveSettings& settings,
                                             double nrGpuMs,
                                             double frameGpuMs,
                                             double sourceFps,
                                             double processedFps,
                                             double queuePressure,
                                             double asyncOverlap,
                                             FrameTimingSource frameTimingSource) {
    std::scoped_lock lock(mutex_);
    const AdaptiveSettings normalized = NormalizeSettings(settings);
    ReconfigureIfNeeded(normalized);
    if (pipeline_.ReconfigurePending()) {
        // Resource drain is deliberate backpressure, not evidence that the selected scale is too
        // expensive. Freeze adaptive decisions until every old slot is released/reconfigured.
        lastDecision_.workingScale = compatibilityOverride_.value_or(runtime_.WorkingScale());
        lastDecision_.changedScale = false;
        return lastDecision_.workingScale;
    }

    if (!normalized.enabled) {
        lastDecision_ = {};
        lastDecision_.workingScale = normalized.fixedScale;
        return lastDecision_.workingScale;
    }

    if (compatibilityOverride_.has_value()) {
        // A native-scale compatibility escape is not part of the preset ladder. Do not let
        // measurements collected at the override train or move a controller whose internal rung
        // still describes the reduced-resolution ladder. Resume adaptation only after the host
        // explicitly clears the resource-build quarantine.
        lastDecision_.workingScale = *compatibilityOverride_;
        lastDecision_.changedScale = false;
        return lastDecision_.workingScale;
    }

    const auto now = std::chrono::steady_clock::now();
    double dt = 1.0 / 60.0;
    if (haveTimestamp_) {
        dt = std::chrono::duration<double>(now - lastUpdate_).count();
        dt = std::clamp(dt, 1.0 / 1000.0, 0.10);
    }
    lastUpdate_ = now;
    haveTimestamp_ = true;

    TelemetrySample t;
    t.dtSeconds = dt;
    t.nrGpuMs = nrGpuMs;
    t.nrTimingFresh = std::isfinite(nrGpuMs) && nrGpuMs > 0.0;
    t.frameGpuMs = frameGpuMs;
    t.frameTimingSource = frameTimingSource;
    const auto tracked = telemetry_.BuildSample(dt, nrGpuMs, frameGpuMs, asyncOverlap,
                                                0.0, 0.0, false, false, false, NowSeconds());
    t.sourceFps = std::isfinite(sourceFps) && sourceFps > 0.0 ? sourceFps : tracked.sourceFps;
    t.processedFps = std::isfinite(processedFps) && processedFps > 0.0 ? processedFps : tracked.processedFps;
    // Serial timestamp latency is not queue pressure. Only an executor with a real bounded queue
    // should pass a non-zero pressure value here.
    const double externalPressure = std::isfinite(queuePressure)
                                        ? std::clamp(queuePressure, 0.0, 1.0)
                                        : 0.0;
    t.queuePressure = std::max(externalPressure, pipeline_.QueuePressure());
    t.asyncOverlap = std::isfinite(asyncOverlap) ? std::clamp(asyncOverlap, 0.0, 1.0) : 0.0;

    lastTelemetrySample_ = t;
    lastDecision_ = runtime_.OnTelemetry(t);
    if (lastDecision_.changedScale)
        AdvanceScaleGenerationLocked();
    if (compatibilityOverride_.has_value()) {
        lastDecision_.workingScale = *compatibilityOverride_;
        lastDecision_.changedScale = false;
    }
    return lastDecision_.workingScale;
}

PerformanceDecision OptiScalerAdapter::LastDecision() const {
    std::scoped_lock lock(mutex_);
    return lastDecision_;
}

AutoDecision OptiScalerAdapter::ResolveAuto(
    const GameContext& game, const FrameContext& frame,
    const RuntimeCapabilities& capabilities,
    const AdaptiveSettings& settings,
    double nrGpuMs,
    double frameGpuMs,
    double sourceFps,
    double processedFps,
    double queuePressure,
    double asyncOverlap,
    SchedulerMode requestedScheduler,
    unsigned generationMultiplier,
    const CompatibilityOverride* compatibility,
    FrameTimingSource frameTimingSource) {
    std::scoped_lock lock(mutex_);
    const AdaptiveSettings normalized = NormalizeSettings(settings);
    ReconfigureIfNeeded(normalized);

    const auto now = std::chrono::steady_clock::now();
    double dt = 1.0 / 60.0;
    if (haveTimestamp_) {
        dt = std::chrono::duration<double>(now - lastUpdate_).count();
        dt = std::clamp(dt, 1.0 / 1000.0, 0.10);
    }
    lastUpdate_ = now;
    haveTimestamp_ = true;

    TelemetrySample t;
    t.dtSeconds = dt;
    t.nrGpuMs = nrGpuMs;
    t.nrTimingFresh = std::isfinite(nrGpuMs) && nrGpuMs > 0.0;
    t.frameGpuMs = frameGpuMs;
    t.frameTimingSource = frameTimingSource;
    const auto tracked = telemetry_.BuildSample(dt, nrGpuMs, frameGpuMs, asyncOverlap,
                                                0.0, 0.0, false, false, false, NowSeconds());
    t.sourceFps = std::isfinite(sourceFps) && sourceFps > 0.0 ? sourceFps : tracked.sourceFps;
    t.processedFps = std::isfinite(processedFps) && processedFps > 0.0 ? processedFps : tracked.processedFps;
    const double externalPressure = std::isfinite(queuePressure)
                                        ? std::clamp(queuePressure, 0.0, 1.0)
                                        : 0.0;
    t.queuePressure = std::max(externalPressure, pipeline_.QueuePressure());
    t.asyncOverlap = std::isfinite(asyncOverlap) ? std::clamp(asyncOverlap, 0.0, 1.0) : 0.0;
    t.asyncComputeAvailable = capabilities.asyncCompute;
    t.secondaryGpuAvailable = capabilities.secondaryGpu;
    t.fgEnabled = game.frameGeneration;

    lastTelemetrySample_ = t;

    const auto runtimeEpochBefore = runtime_.AutoConfigurationGeneration();
    const bool previouslySupported = lastAutoDecision_.supported;
    AutoDecision decision = runtime_.ResolveAuto(game, frame, t, capabilities,
                                                 requestedScheduler, generationMultiplier,
                                                 compatibility);

    // Work tickets are owned by the adapter, so mirror every central Auto epoch transition here.
    // This invalidates late timings when the central runtime changes shape, scheduler, precision or
    // WorkingScale, even though the selected scale may numerically remain unchanged.
    const bool runtimeEpochChanged = runtime_.AutoConfigurationGeneration() != runtimeEpochBefore;
    if (runtimeEpochChanged || previouslySupported != decision.supported ||
        decision.performance.changedScale)
        AdvanceScaleGenerationLocked();

    if (compatibilityOverride_.has_value()) {
        decision.workingScale = *compatibilityOverride_;
        decision.performance.workingScale = *compatibilityOverride_;
    } else if (!normalized.enabled) {
        decision.workingScale = normalized.fixedScale;
        decision.performance.workingScale = normalized.fixedScale;
    }

    lastCapsFp8_ = capabilities.fp8;
    lastCapsHybridNvfp4_ = capabilities.hybridNvfp4;
    lastAutoDecision_ = decision;
    lastDecision_ = decision.performance;
    return decision;
}

AutoDecision OptiScalerAdapter::LastAutoDecision() const {
    std::scoped_lock lock(mutex_);
    return lastAutoDecision_;
}

std::vector<NrPrecision> OptiScalerAdapter::SupportedPrecisions() const {
    std::scoped_lock lock(mutex_);
    return nrfusion::SupportedPrecisions(lastCapsFp8_, lastCapsHybridNvfp4_);
}

FusionRuntime& OptiScalerAdapter::FusionRuntimeEngine() noexcept {
    return runtime_;
}

const FusionRuntime& OptiScalerAdapter::FusionRuntimeEngine() const noexcept {
    return runtime_;
}

std::string OptiScalerAdapter::DiagnosticsText() const {
    std::scoped_lock lock(mutex_);
    const auto precision = precisionTuner_.Result();
    const auto async = asyncTuner_.Result();
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    out << "NR Fusion adapter diagnostics\n";
    out << "mode=" << static_cast<unsigned>(settings_.mode)
        << " modeName=" << UserModeName(settings_.mode)
        << " enabled=" << (settings_.enabled ? 1 : 0)
        << " targetFps=" << settings_.targetFps
        << " scaleGeneration=" << scaleGeneration_ << '\n';
    const auto& performanceConfig = runtime_.PerformanceCfg();
    out << "scaleRange=" << performanceConfig.minScale << ".." << performanceConfig.maxScale
        << " nrBudgetFraction=" << performanceConfig.nrBudgetFrameFraction
        << " frameNrMaterialRatio=" << performanceConfig.frameNrMaterialRatio
        << " nrWarmupSamples=" << performanceConfig.nrWarmupSamples << '\n';
    out << "workingScale=" << lastDecision_.workingScale
        << " changedScale=" << (lastDecision_.changedScale ? 1 : 0)
        << " overBudget=" << (lastDecision_.overBudget ? 1 : 0)
        << " headroom=" << (lastDecision_.hasHeadroom ? 1 : 0)
        << " telemetryReady=" << (lastDecision_.telemetryReady ? 1 : 0)
        << " adaptiveState=" << AdaptiveStateName(lastDecision_.state)
        << " sourceCapFps=" << lastDecision_.recommendedSourceCapFps << '\n';
    out << "nrCriticalMs=" << lastDecision_.effectiveNrCriticalMs
        << " nrBudgetMs=" << lastDecision_.resolvedNrBudgetMs
        << " nrGpuMs=" << lastTelemetrySample_.nrGpuMs
        << " nrTimingFresh=" << (lastTelemetrySample_.nrTimingFresh ? 1 : 0)
        << " frameGpuMs=" << lastTelemetrySample_.frameGpuMs
        << " frameTimingSource=" << FrameTimingSourceName(lastTelemetrySample_.frameTimingSource) << '\n';
    out << "sourceWorkFps=" << lastTelemetrySample_.sourceFps
        << " processedWorkFps=" << lastTelemetrySample_.processedFps
        << " queuePressure=" << lastTelemetrySample_.queuePressure
        << " asyncOverlap=" << lastTelemetrySample_.asyncOverlap << '\n';
    out << "autoScheduler=" << SchedulerModeName(lastAutoDecision_.scheduler)
        << " autoPrecision=" << PrecisionName(lastAutoDecision_.precision)
        << " presentation=" << static_cast<unsigned>(lastAutoDecision_.presentation)
        << " multiplier=" << lastAutoDecision_.generationMultiplier << '\n';
    out << "pipelineQueue=" << pipeline_.QueuePressure()
        << " reconfigurePending=" << (pipeline_.ReconfigurePending() ? 1 : 0)
        << " precisionState=" << static_cast<unsigned>(precisionTuner_.State())
        << " precisionQualified=" << (precision.candidateQualified ? 1 : 0)
        << " precisionQualityAccepted=" << (precision.qualityAccepted ? 1 : 0)
        << " precisionQualitySamples=" << precision.qualityEvidenceSamples << '\n';
    out << "asyncState=" << static_cast<unsigned>(async.state)
        << " asyncQualified=" << (async.qualified ? 1 : 0)
        << " asyncMedianGain=" << async.medianGain
        << " asyncP95Regression=" << async.p95Regression << '\n';
    return out.str();
}

WorkTicket OptiScalerAdapter::BeginNrWork(std::uint64_t sourceFrame, std::uint64_t viewKey,
                                                float workingScale, std::uint8_t precisionTag) {
    std::scoped_lock lock(mutex_);
    auto ticket = works_.Begin(sourceFrame, viewKey, scaleGeneration_, SanitizeScale(workingScale), precisionTag);
    telemetry_.OnSourceWork(NowSeconds());
    return ticket;
}

bool OptiScalerAdapter::SubmitNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Submit(ticket)) return false;
    telemetry_.OnNrSubmitted();
    return true;
}

bool OptiScalerAdapter::AbandonNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Abandon(ticket)) return false;
    qualityEvidenceByWork_.erase(ticket.id);
    telemetry_.OnNrAbandoned();
    return true;
}

bool OptiScalerAdapter::CompleteNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Complete(ticket)) return false;
    qualityEvidenceByWork_.erase(ticket.id);
    telemetry_.OnNrCompleted(NowSeconds());
    return true;
}

bool OptiScalerAdapter::ReportQualityEvidence(const WorkTicket& ticket,
                                              const QualityEvidence& quality) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket) || !quality.WellFormed()) return false;
    qualityEvidenceByWork_[ticket.id] = quality;
    return true;
}

std::optional<PipelineTicket> OptiScalerAdapter::AdmitPipelinedWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    auto pipelineTicket = pipeline_.TrySubmit(ticket.id, ticket.sourceFrame, ticket.viewKey);
    if (!pipelineTicket) return std::nullopt;
    if (!works_.Submit(ticket)) {
        pipeline_.Abandon(*pipelineTicket);
        return std::nullopt;
    }
    telemetry_.OnNrSubmitted();
    return pipelineTicket;
}

bool OptiScalerAdapter::CompletePipelinedWork(const PipelineTicket& pipelineTicket,
                                               const WorkTicket& workTicket) {
    std::scoped_lock lock(mutex_);
    if (pipelineTicket.workId != workTicket.id || pipelineTicket.frameId != workTicket.sourceFrame ||
        pipelineTicket.viewKey != workTicket.viewKey || !works_.IsSubmitted(workTicket)) return false;
    if (!pipeline_.Complete(pipelineTicket)) return false;
    // IsSubmitted above guarantees the exact ledger ticket. Complete cannot fail without an
    // internal invariant violation because the adapter holds the same mutex across both mutations.
    if (!works_.Complete(workTicket)) {
        pipeline_.Abandon(pipelineTicket);
        return false;
    }
    qualityEvidenceByWork_.erase(workTicket.id);
    telemetry_.OnNrCompleted(NowSeconds());
    return true;
}

bool OptiScalerAdapter::AbandonPipelinedWork(const PipelineTicket& pipelineTicket,
                                              const WorkTicket& workTicket) {
    std::scoped_lock lock(mutex_);
    if (pipelineTicket.workId != workTicket.id ||
        pipelineTicket.frameId != workTicket.sourceFrame ||
        pipelineTicket.viewKey != workTicket.viewKey ||
        !works_.IsSubmitted(workTicket)) return false;
    if (!pipeline_.Abandon(pipelineTicket)) return false;
    if (!works_.Abandon(workTicket)) return false;
    qualityEvidenceByWork_.erase(workTicket.id);
    telemetry_.OnNrAbandoned();
    return true;
}

std::optional<PipelineReadyFrame> OptiScalerAdapter::ConsumePipelinedBefore(std::uint64_t currentFrame,
                                                                            std::uint64_t viewKey) {
    std::scoped_lock lock(mutex_);
    return pipeline_.ConsumeLatestBefore(currentFrame, viewKey);
}

void OptiScalerAdapter::RequestPipelineReconfigure() {
    std::scoped_lock lock(mutex_);
    pipeline_.RequestReconfigure();
}

std::size_t OptiScalerAdapter::DiscardPipelineReadyForReconfigure() {
    std::scoped_lock lock(mutex_);
    return pipeline_.DiscardReadyForReconfigure();
}

bool OptiScalerAdapter::ApplyPipelineReconfigureIfIdle() {
    std::scoped_lock lock(mutex_);
    return pipeline_.ApplyReconfigureIfIdle();
}

double OptiScalerAdapter::PipelineQueuePressure() const {
    std::scoped_lock lock(mutex_);
    return pipeline_.QueuePressure();
}

bool OptiScalerAdapter::PipelineReconfigurePending() const {
    std::scoped_lock lock(mutex_);
    return pipeline_.ReconfigurePending();
}

bool OptiScalerAdapter::MapTimedWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket)) return false;
    if (const auto dropped = timingMap_.Push(ticket); dropped && works_.Abandon(*dropped))
        telemetry_.OnNrAbandoned();
    return true;
}

void OptiScalerAdapter::MapInvalidTimedAttempt() {
    std::scoped_lock lock(mutex_);
    if (const auto dropped = timingMap_.PushInvalid(); dropped && works_.Abandon(*dropped))
        telemetry_.OnNrAbandoned();
}

bool OptiScalerAdapter::RetireTimedInterval(double gpuMs) {
    std::scoped_lock lock(mutex_);
    const auto mapped = timingMap_.Pop();
    if (!mapped || !mapped->mapsWork) return false;
    if (!works_.Complete(mapped->ticket)) return false;
    telemetry_.OnNrCompleted(NowSeconds());
    AcceptTimingLocked(mapped->ticket, gpuMs);
    return true;
}

bool OptiScalerAdapter::MapTimedSlot(std::size_t slot, const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket)) return false;
    auto result = timingSlots_.Assign(slot, ticket);
    if (!result.accepted) return false;
    if (result.displaced && works_.Abandon(*result.displaced)) telemetry_.OnNrAbandoned();
    return true;
}

bool OptiScalerAdapter::RetireTimedSlot(std::size_t slot, double gpuMs) {
    std::scoped_lock lock(mutex_);
    const auto ticket = timingSlots_.Take(slot);
    if (!ticket || !works_.Complete(*ticket)) return false;
    telemetry_.OnNrCompleted(NowSeconds());
    AcceptTimingLocked(*ticket, gpuMs);
    return true;
}

void OptiScalerAdapter::ClearTimedSlot(std::size_t slot) {
    std::scoped_lock lock(mutex_);
    if (const auto ticket = timingSlots_.Clear(slot); ticket && works_.Abandon(*ticket))
        telemetry_.OnNrAbandoned();
}

NrPrecision OptiScalerAdapter::ResolvePrecision(bool automaticEnabled, bool candidateAvailable,
                                                     NrPrecision manualPrecision) {
    std::scoped_lock lock(mutex_);
    if (!automaticEnabled) {
        lastAutoDecision_.precision = manualPrecision;
        return manualPrecision;
    }
    const int key = PrecisionScaleKey(lastDecision_.workingScale > 0.0f
                                          ? lastDecision_.workingScale
                                          : runtime_.WorkingScale());
    if (const auto it = precisionByScale_.find(key); it != precisionByScale_.end()) {
        const auto resolved = it->second ? NrPrecision::HybridNvfp4 : NrPrecision::Fp8;
        lastAutoDecision_.precision = resolved;
        return resolved;
    }

    if (!candidateAvailable) {
        const auto state = precisionTuner_.State();
        if (state == PrecisionAutotuneState::CandidateWarmup ||
            state == PrecisionAutotuneState::CandidateMeasure ||
            state == PrecisionAutotuneState::PreferNvfp4) {
            precisionTuner_.ReportCandidateFailure(scaleGeneration_);
            CacheFinishedPrecisionLocked(lastDecision_.workingScale > 0.0f
                                             ? lastDecision_.workingScale
                                             : runtime_.WorkingScale());
        }
        lastAutoDecision_.precision = NrPrecision::Fp8;
        return NrPrecision::Fp8;
    }
    const auto resolved = precisionTuner_.Desired(true, true, NrPrecision::Fp8);
    lastAutoDecision_.precision = resolved;
    return resolved;
}

void OptiScalerAdapter::ReportPrecisionCandidateFailure() {
    std::scoped_lock lock(mutex_);
    const auto before = precisionTuner_.State();
    precisionTuner_.ReportCandidateFailure(scaleGeneration_);
    if (before != precisionTuner_.State() &&
        precisionTuner_.State() == PrecisionAutotuneState::CandidateFailed)
    {
        const float scale = lastDecision_.workingScale > 0.0f ? lastDecision_.workingScale
                                                              : runtime_.WorkingScale();
        precisionByScale_[PrecisionScaleKey(scale)] = false;
    }
}

PrecisionAutotuneResult OptiScalerAdapter::PrecisionStatus() const {
    std::scoped_lock lock(mutex_);
    return precisionTuner_.Result();
}

bool OptiScalerAdapter::PrecisionCandidateRequested() const {
    std::scoped_lock lock(mutex_);
    const auto state = precisionTuner_.State();
    return state == PrecisionAutotuneState::CandidateWarmup ||
           state == PrecisionAutotuneState::CandidateMeasure ||
           state == PrecisionAutotuneState::PreferNvfp4;
}

bool OptiScalerAdapter::UpdateQueueClock(QueueClockId queue, const QueueClockCalibrationSample& sample) {
    std::scoped_lock lock(mutex_);
    const bool wasStable = queueClocks_.Stable(queue);
    const bool updated = queueClocks_.Update(queue, sample);
    if (updated && wasStable && !queueClocks_.Stable(queue)) {
        // A queue/device clock discontinuity invalidates previously measured cross-queue overlap.
        // Never allow the async qualifier to inherit a value from the old timestamp epoch.
        asyncOverlap_.Reset();
        asyncTuner_.Reset();
        freshAsyncOverlapSampleId_ = 0;
    }
    return updated;
}

std::optional<double> OptiScalerAdapter::ObserveAsyncOverlap(
    std::uint64_t sampleId, const QueueGpuIntervalTicks& nr,
    const std::vector<QueueGpuIntervalTicks>& concurrent, double dtSeconds) {
    std::scoped_lock lock(mutex_);
    if (sampleId == 0) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    const auto nrCommon = queueClocks_.ToCommonInterval(nr.queue, nr.startGpuTimestamp, nr.endGpuTimestamp);
    if (!nrCommon) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    std::vector<GpuInterval> common;
    common.reserve(concurrent.size());
    for (const auto& interval : concurrent) {
        const auto mapped = queueClocks_.ToCommonInterval(interval.queue, interval.startGpuTimestamp,
                                                           interval.endGpuTimestamp);
        if (mapped) common.push_back(*mapped);
    }
    if (common.empty()) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    const double overlap = asyncOverlap_.Update(*nrCommon, common, dtSeconds);
    freshAsyncOverlapSampleId_ = sampleId;
    return overlap;
}

void OptiScalerAdapter::ObserveAsyncTrial(std::uint64_t sampleId, SchedulerMode mode,
                                          double frameGpuMs, double queuePressure,
                                          FrameTimingSource frameTimingSource) {
    std::scoped_lock lock(mutex_);
    // Serialized baseline needs only frame time. Every async-trial frame, however, must have a
    // calibrated overlap observation from that frame; stale overlap is worse than no sample.
    if (mode == SchedulerMode::AsyncCompute &&
        (sampleId == 0 || freshAsyncOverlapSampleId_ != sampleId)) return;
    TelemetrySample sample;
    sample.frameGpuMs = frameGpuMs;
    sample.frameTimingSource = frameTimingSource;
    sample.asyncOverlap = asyncOverlap_.Value();
    sample.queuePressure = std::isfinite(queuePressure) ? std::clamp(queuePressure, 0.0, 1.0) : 1.0;
    asyncTuner_.Observe(mode, sample);
    freshAsyncOverlapSampleId_ = 0;
}

SchedulerMode OptiScalerAdapter::QualifiedScheduler(bool asyncAvailable) const {
    std::scoped_lock lock(mutex_);
    return asyncTuner_.NextMode(asyncAvailable);
}

AsyncQualificationResult OptiScalerAdapter::AsyncStatus() const {
    std::scoped_lock lock(mutex_);
    return asyncTuner_.Result();
}

QueueClockCalibrationStatus OptiScalerAdapter::QueueClockStatus(QueueClockId queue) const {
    std::scoped_lock lock(mutex_);
    return queueClocks_.Status(queue);
}

double OptiScalerAdapter::TrackedSourceFps() const {
    std::scoped_lock lock(mutex_);
    return telemetry_.CurrentSourceFps(NowSeconds());
}

double OptiScalerAdapter::TrackedProcessedFps() const {
    std::scoped_lock lock(mutex_);
    return telemetry_.CurrentProcessedFps(NowSeconds());
}

double OptiScalerAdapter::TrackedNrGpuMs() const {
    std::scoped_lock lock(mutex_);
    return haveMatchedNrGpuMs_ ? matchedNrGpuMs_ : 0.0;
}

double OptiScalerAdapter::ConsumeTrackedNrGpuMs() {
    std::scoped_lock lock(mutex_);
    if (!haveMatchedNrGpuMs_ || consumedNrTimingSequence_ == matchedNrTimingSequence_)
        return 0.0;
    consumedNrTimingSequence_ = matchedNrTimingSequence_;
    return matchedNrGpuMs_;
}

std::uint64_t OptiScalerAdapter::ScaleGeneration() const {
    std::scoped_lock lock(mutex_);
    return scaleGeneration_;
}

std::optional<float> OptiScalerAdapter::ReportWorkingScaleBuildFailure(float failedScale) {
    std::scoped_lock lock(mutex_);
    if (std::isfinite(failedScale) &&
        std::none_of(failedWorkingScales_.begin(), failedWorkingScales_.end(), [failedScale](float v) {
            return std::fabs(v - failedScale) < 1e-4f;
        }))
        failedWorkingScales_.push_back(failedScale);
    auto fallback = runtime_.ReportScaleBuildFailure(failedScale);
    if (!fallback && settings_.enabled && std::isfinite(failedScale) && failedScale < 0.999f) {
        // Some performance presets intentionally omit 100%. If every in-preset higher rung is gone,
        // leave the preset rather than disabling NR: native work resolution is the compatibility escape.
        compatibilityOverride_ = 1.0f;
        // ResolveWorkingScale freezes while the compatibility escape is active. Discard the old
        // wall-clock anchor too so clearing the quarantine cannot turn the time spent at 100% into
        // artificial residency/EMA time for the reduced-resolution controller.
        haveTimestamp_ = false;
        fallback = 1.0f;
    }
    if (fallback) {
        if (std::fabs(lastDecision_.workingScale - *fallback) > 1e-4f)
            AdvanceScaleGenerationLocked();
        lastDecision_.workingScale = *fallback;
        lastDecision_.changedScale = true;
    }
    return fallback;
}

void OptiScalerAdapter::ClearWorkingScaleBuildFailures() {
    std::scoped_lock lock(mutex_);
    const float oldEffectiveScale = compatibilityOverride_.value_or(runtime_.WorkingScale());
    failedWorkingScales_.clear();
    compatibilityOverride_.reset();
    runtime_.ClearScaleBuildFailures();
    const float resumedScale = runtime_.WorkingScale();
    if (std::fabs(oldEffectiveScale - resumedScale) > 1e-4f)
        AdvanceScaleGenerationLocked();
    haveTimestamp_ = false;
    lastDecision_.workingScale = resumedScale;
    lastDecision_.changedScale = std::fabs(oldEffectiveScale - resumedScale) > 1e-4f;
}

void OptiScalerAdapter::Reset(float initialScale) {
    std::scoped_lock lock(mutex_);
    failedWorkingScales_.clear();
    compatibilityOverride_.reset();
    runtime_.ClearScaleBuildFailures();
    runtime_.ClearLearnedCostModel();
    runtime_.Reset(initialScale);
    matchedNrTimingSequence_ = 0;
    consumedNrTimingSequence_ = 0;
    precisionByScale_.clear();
    AdvanceScaleGenerationLocked();
    works_.ResetSession();
    timingMap_.Reset();
    timingSlots_.Reset();
    qualityEvidenceByWork_.clear();
    telemetry_.Reset();
    pipeline_.Reset();
    queueClocks_.Reset();
    asyncOverlap_.Reset();
    asyncTuner_.Reset();
    freshAsyncOverlapSampleId_ = 0;
    lastDecision_ = {};
    lastDecision_.workingScale = runtime_.WorkingScale();
    lastAutoDecision_ = {};
    lastTelemetrySample_ = {};
    haveTimestamp_ = false;
}

void OptiScalerAdapter::ResetForShape(const AdaptiveSettings& settings) {
    std::scoped_lock lock(mutex_);
    const AdaptiveSettings normalized = NormalizeSettings(settings);
    ReconfigureIfNeeded(normalized);
    failedWorkingScales_.clear();
    compatibilityOverride_.reset();
    runtime_.ClearScaleBuildFailures();
    runtime_.ClearLearnedCostModel();
    const float initScale = normalized.enabled ? runtime_.PerformanceCfg().maxScale
                                               : normalized.fixedScale;
    runtime_.Reset(initScale);
    matchedNrTimingSequence_ = 0;
    consumedNrTimingSequence_ = 0;
    precisionByScale_.clear();
    AdvanceScaleGenerationLocked();
    works_.ResetSession();
    timingMap_.Reset();
    timingSlots_.Reset();
    qualityEvidenceByWork_.clear();
    telemetry_.Reset();
    pipeline_.Reset();
    queueClocks_.Reset();
    asyncOverlap_.Reset();
    asyncTuner_.Reset();
    freshAsyncOverlapSampleId_ = 0;
    lastDecision_ = {};
    lastDecision_.workingScale = normalized.enabled ? runtime_.WorkingScale()
                                                    : normalized.fixedScale;
    lastAutoDecision_ = {};
    haveTimestamp_ = false;
}

} // namespace nrfusion
