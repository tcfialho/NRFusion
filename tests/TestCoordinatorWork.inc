PerformanceDecision TestCoordinator::LastDecision() const {
    std::scoped_lock lock(mutex_);
    return lastDecision_;
}

AutoDecision TestCoordinator::ResolveAuto(
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

AutoDecision TestCoordinator::LastAutoDecision() const {
    std::scoped_lock lock(mutex_);
    return lastAutoDecision_;
}

std::vector<NrPrecision> TestCoordinator::SupportedPrecisions() const {
    std::scoped_lock lock(mutex_);
    std::vector<NrPrecision> result;
    if (lastCapsFp8_) result.push_back(NrPrecision::Fp8);
    if (lastCapsHybridNvfp4_) result.push_back(NrPrecision::HybridNvfp4);
    return result;
}

FusionRuntime& TestCoordinator::FusionRuntimeEngine() noexcept { return runtime_; }
const FusionRuntime& TestCoordinator::FusionRuntimeEngine() const noexcept { return runtime_; }

std::string TestCoordinator::DiagnosticsText() const {
    std::scoped_lock lock(mutex_);
    std::ostringstream out;
    const auto precision = precisionTuner_.Result();
    const auto async = asyncTuner_.Result();
    out << "NRFusion Coordinator Diagnostics\n"
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

WorkTicket TestCoordinator::BeginNrWork(std::uint64_t sourceFrame, std::uint64_t viewKey,
                                         float workingScale, std::uint8_t precisionTag) {
    std::scoped_lock lock(mutex_);
    auto ticket = works_.Begin(sourceFrame, viewKey, scaleGeneration_, SanitizeScale(workingScale), precisionTag);
    telemetry_.OnSourceWork(NowSeconds());
    return ticket;
}

bool TestCoordinator::SubmitNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Submit(ticket)) return false;
    telemetry_.OnNrSubmitted();
    return true;
}

bool TestCoordinator::AbandonNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Abandon(ticket)) return false;
    qualityEvidenceByWork_.erase(ticket.id);
    telemetry_.OnNrAbandoned();
    return true;
}

bool TestCoordinator::CompleteNrWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.Complete(ticket)) return false;
    qualityEvidenceByWork_.erase(ticket.id);
    telemetry_.OnNrCompleted(NowSeconds());
    return true;
}

bool TestCoordinator::ReportQualityEvidence(const WorkTicket& ticket,
                                             const QualityEvidence& quality) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket) || !quality.WellFormed()) return false;
    qualityEvidenceByWork_[ticket.id] = quality;
    return true;
}

std::optional<PipelineTicket> TestCoordinator::AdmitPipelinedWork(const WorkTicket& ticket) {
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

bool TestCoordinator::CompletePipelinedWork(const PipelineTicket& pipelineTicket,
                                             const WorkTicket& workTicket) {
    std::scoped_lock lock(mutex_);
    if (pipelineTicket.workId != workTicket.id || pipelineTicket.frameId != workTicket.sourceFrame ||
        pipelineTicket.viewKey != workTicket.viewKey || !works_.IsSubmitted(workTicket)) return false;
    if (!pipeline_.Complete(pipelineTicket)) return false;
    if (!works_.Complete(workTicket)) {
        pipeline_.Abandon(pipelineTicket);
        return false;
    }
    qualityEvidenceByWork_.erase(workTicket.id);
    telemetry_.OnNrCompleted(NowSeconds());
    return true;
}

bool TestCoordinator::AbandonPipelinedWork(const PipelineTicket& pipelineTicket,
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

std::optional<PipelineReadyFrame> TestCoordinator::ConsumePipelinedBefore(std::uint64_t currentFrame,
                                                                          std::uint64_t viewKey) {
    std::scoped_lock lock(mutex_);
    return pipeline_.ConsumeLatestBefore(currentFrame, viewKey);
}

void TestCoordinator::RequestPipelineReconfigure() {
    std::scoped_lock lock(mutex_);
    pipeline_.RequestReconfigure();
}

std::size_t TestCoordinator::DiscardPipelineReadyForReconfigure() {
    std::scoped_lock lock(mutex_);
    return pipeline_.DiscardReadyForReconfigure();
}

bool TestCoordinator::ApplyPipelineReconfigureIfIdle() {
    std::scoped_lock lock(mutex_);
    return pipeline_.ApplyReconfigureIfIdle();
}

double TestCoordinator::PipelineQueuePressure() const {
    std::scoped_lock lock(mutex_);
    return pipeline_.QueuePressure();
}

bool TestCoordinator::PipelineReconfigurePending() const {
    std::scoped_lock lock(mutex_);
    return pipeline_.ReconfigurePending();
}
