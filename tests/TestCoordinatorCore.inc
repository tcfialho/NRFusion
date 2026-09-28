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
} // namespace

PerformanceConfig TestCoordinator::ToConfig(const AdaptiveSettings& s) {
    PerformancePreset preset = PerformancePreset::Auto;
    switch (s.mode) {
    case UserMode::MaxFps: preset = PerformancePreset::Performance; break;
    case UserMode::Balanced: preset = PerformancePreset::Balanced; break;
    case UserMode::Quality: preset = PerformancePreset::Quality; break;
    case UserMode::Auto: preset = PerformancePreset::Auto; break;
    case UserMode::Custom:
        return MakePerformanceConfig(PerformancePreset::Auto, SanitizeTargetFps(s.targetFps));
    }
    return MakePerformanceConfig(preset, SanitizeTargetFps(s.targetFps));
}

TestCoordinator& TestCoordinator::Instance() {
    static TestCoordinator instance;
    return instance;
}

TestCoordinator::TestCoordinator()
    : runtime_(ToConfig(settings_)) {
    asyncCommonIntervals_.reserve(16);
    precisionTuner_.Reset(scaleGeneration_);
}

int TestCoordinator::PrecisionScaleKey(float workingScale) {
    if (!std::isfinite(workingScale)) workingScale = 1.0f;
    return static_cast<int>(std::lround(std::clamp(workingScale, 0.25f, 2.0f) * 1000.0f));
}

void TestCoordinator::CacheFinishedPrecisionLocked(float workingScale) {
    const auto result = precisionTuner_.Result();
    if (!result.finished) return;
    precisionByScale_[PrecisionScaleKey(workingScale)] = result.candidateQualified;
}

bool TestCoordinator::SettingsEquivalent(const AdaptiveSettings& a, const AdaptiveSettings& b) {
    auto near = [](double x, double y) { return std::fabs(x - y) < 1e-5; };
    return a.enabled == b.enabled && a.mode == b.mode &&
           near(static_cast<double>(a.fixedScale), static_cast<double>(b.fixedScale)) &&
           near(a.targetFps, b.targetFps);
}

void TestCoordinator::AdvanceScaleGenerationLocked() {
    if (scaleGeneration_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("NRFusion scale generation namespace exhausted");
    ++scaleGeneration_;
    matchedNrGpuMs_ = 0.0;
    haveMatchedNrGpuMs_ = false;
    precisionTuner_.Reset(scaleGeneration_);
}

void TestCoordinator::AcceptTimingLocked(const WorkTicket& ticket, double gpuMs) {
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

void TestCoordinator::ReconfigureIfNeeded(const AdaptiveSettings& s) {
    if (SettingsEquivalent(settings_, s)) return;
    const float previous = runtime_.WorkingScale();
    const double previousTargetFps = runtime_.PerformanceCfg().targetFps;
    const UserMode previousMode = settings_.mode;
    settings_ = s;
    const auto config = ToConfig(settings_);
    runtime_.Reconfigure(config);
    asyncOverlap_.Reset();
    asyncTuner_.Reset();
    freshAsyncOverlapSampleId_ = 0;
    for (float failedScale : failedWorkingScales_) {
        runtime_.ReportScaleBuildFailure(failedScale);
    }
    const bool sameContract = std::fabs(static_cast<double>(config.targetFps) -
                                        static_cast<double>(previousTargetFps)) < 1e-6 &&
                              previousMode == settings_.mode;
    runtime_.Reset(sameContract ? std::clamp(previous, config.minScale, config.maxScale)
                                : config.maxScale);
    AdvanceScaleGenerationLocked();
    haveTimestamp_ = false;
}

float TestCoordinator::ResolveWorkingScale(const AdaptiveSettings& settings,
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
