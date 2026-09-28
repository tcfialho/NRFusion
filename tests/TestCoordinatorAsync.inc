bool TestCoordinator::UpdateQueueClock(QueueClockId queue, const QueueClockCalibrationSample& sample) {
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

std::optional<double> TestCoordinator::ObserveAsyncOverlap(
    std::uint64_t sampleId, const QueueGpuIntervalTicks& nr,
    const std::vector<QueueGpuIntervalTicks>& concurrent, double dtSeconds) {
    std::scoped_lock lock(mutex_);
    if (sampleId == 0) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    const auto nrCommon = queueClocks_.ToCommonInterval(nr.queue, nr.startGpuTimestamp, nr.endGpuTimestamp);
    if (!nrCommon) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    auto mapIntervals = [&](std::vector<GpuInterval>& common) {
        common.clear();
        for (const auto& interval : concurrent) {
            const auto mapped = queueClocks_.ToCommonInterval(
                interval.queue, interval.startGpuTimestamp, interval.endGpuTimestamp);
            if (mapped) common.push_back(*mapped);
        }
    };

    constexpr std::size_t kReusableIntervals = 16;
    if (concurrent.size() <= kReusableIntervals) {
        mapIntervals(asyncCommonIntervals_);
        if (asyncCommonIntervals_.empty()) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
        const double overlap = asyncOverlap_.Update(*nrCommon, asyncCommonIntervals_, dtSeconds);
        freshAsyncOverlapSampleId_ = sampleId;
        return overlap;
    }

    std::vector<GpuInterval> common;
    common.reserve(concurrent.size());
    mapIntervals(common);
    if (common.empty()) { freshAsyncOverlapSampleId_ = 0; return std::nullopt; }
    const double overlap = asyncOverlap_.Update(*nrCommon, common, dtSeconds);
    freshAsyncOverlapSampleId_ = sampleId;
    return overlap;
}

void TestCoordinator::ObserveAsyncTrial(std::uint64_t sampleId, SchedulerMode mode,
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

SchedulerMode TestCoordinator::QualifiedScheduler(bool asyncAvailable) const {
    std::scoped_lock lock(mutex_);
    return asyncTuner_.NextMode(asyncAvailable);
}

AsyncQualificationResult TestCoordinator::AsyncStatus() const {
    std::scoped_lock lock(mutex_);
    return asyncTuner_.Result();
}

QueueClockCalibrationStatus TestCoordinator::QueueClockStatus(QueueClockId queue) const {
    std::scoped_lock lock(mutex_);
    return queueClocks_.Status(queue);
}

double TestCoordinator::TrackedSourceFps() const {
    std::scoped_lock lock(mutex_);
    return telemetry_.CurrentSourceFps(NowSeconds());
}

double TestCoordinator::TrackedProcessedFps() const {
    std::scoped_lock lock(mutex_);
    return telemetry_.CurrentProcessedFps(NowSeconds());
}

double TestCoordinator::TrackedNrGpuMs() const {
    std::scoped_lock lock(mutex_);
    return haveMatchedNrGpuMs_ ? matchedNrGpuMs_ : 0.0;
}

double TestCoordinator::ConsumeTrackedNrGpuMs() {
    std::scoped_lock lock(mutex_);
    if (!haveMatchedNrGpuMs_ || consumedNrTimingSequence_ == matchedNrTimingSequence_)
        return 0.0;
    consumedNrTimingSequence_ = matchedNrTimingSequence_;
    return matchedNrGpuMs_;
}

std::uint64_t TestCoordinator::ScaleGeneration() const {
    std::scoped_lock lock(mutex_);
    return scaleGeneration_;
}

std::optional<float> TestCoordinator::ReportWorkingScaleBuildFailure(float failedScale) {
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

void TestCoordinator::ClearWorkingScaleBuildFailures() {
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

void TestCoordinator::Reset(float initialScale) {
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

void TestCoordinator::ResetForShape(const AdaptiveSettings& settings) {
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
