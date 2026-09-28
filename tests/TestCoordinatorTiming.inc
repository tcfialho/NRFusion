bool TestCoordinator::MapTimedWork(const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket)) return false;
    if (const auto dropped = timingMap_.Push(ticket); dropped && works_.Abandon(*dropped))
        telemetry_.OnNrAbandoned();
    return true;
}

void TestCoordinator::MapInvalidTimedAttempt() {
    std::scoped_lock lock(mutex_);
    if (const auto dropped = timingMap_.PushInvalid(); dropped && works_.Abandon(*dropped))
        telemetry_.OnNrAbandoned();
}

bool TestCoordinator::RetireTimedInterval(double gpuMs) {
    std::scoped_lock lock(mutex_);
    const auto mapped = timingMap_.Pop();
    if (!mapped || !mapped->mapsWork) return false;
    if (!works_.Complete(mapped->ticket)) return false;
    telemetry_.OnNrCompleted(NowSeconds());
    AcceptTimingLocked(mapped->ticket, gpuMs);
    return true;
}

bool TestCoordinator::MapTimedSlot(std::size_t slot, const WorkTicket& ticket) {
    std::scoped_lock lock(mutex_);
    if (!works_.IsSubmitted(ticket)) return false;
    auto result = timingSlots_.Assign(slot, ticket);
    if (!result.accepted) return false;
    if (result.displaced && works_.Abandon(*result.displaced)) telemetry_.OnNrAbandoned();
    return true;
}

bool TestCoordinator::RetireTimedSlot(std::size_t slot, double gpuMs) {
    std::scoped_lock lock(mutex_);
    const auto ticket = timingSlots_.Take(slot);
    if (!ticket || !works_.Complete(*ticket)) return false;
    telemetry_.OnNrCompleted(NowSeconds());
    AcceptTimingLocked(*ticket, gpuMs);
    return true;
}

void TestCoordinator::ClearTimedSlot(std::size_t slot) {
    std::scoped_lock lock(mutex_);
    if (const auto ticket = timingSlots_.Clear(slot); ticket && works_.Abandon(*ticket))
        telemetry_.OnNrAbandoned();
}

NrPrecision TestCoordinator::ResolvePrecision(bool automaticEnabled, bool candidateAvailable,
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

void TestCoordinator::ReportPrecisionCandidateFailure() {
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

PrecisionAutotuneResult TestCoordinator::PrecisionStatus() const {
    std::scoped_lock lock(mutex_);
    return precisionTuner_.Result();
}

bool TestCoordinator::PrecisionCandidateRequested() const {
    std::scoped_lock lock(mutex_);
    const auto state = precisionTuner_.State();
    return state == PrecisionAutotuneState::CandidateWarmup ||
           state == PrecisionAutotuneState::CandidateMeasure ||
           state == PrecisionAutotuneState::PreferNvfp4;
}
