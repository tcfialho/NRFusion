#include "nrfusion/AdaptiveExposureController.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

namespace {
constexpr double kDefaultDt = 1.0 / 60.0;

bool MeasurementConfident(const std::optional<ExposureMeasurement>& m, float threshold) {
    if (!m.has_value() || !m->valid)
        return false;
    if (!std::isfinite(m->whitePoint) || m->whitePoint <= 0.0f)
        return false;
    if (!std::isfinite(m->confidence))
        return false;
    return m->confidence >= threshold;
}
} // namespace

AdaptiveExposureController::AdaptiveExposureController(AdaptiveExposureConfig config) {
    SetConfig(config);
}

void AdaptiveExposureController::SetConfig(AdaptiveExposureConfig config) {
    if (config.promoteSustainFrames < 1)
        config.promoteSustainFrames = 1;
    if (config.dropSustainFrames < 1)
        config.dropSustainFrames = 1;
    if (!std::isfinite(config.minSourceConfidence))
        config.minSourceConfidence = 0.5f;
    config.minSourceConfidence = std::clamp(config.minSourceConfidence, 0.0f, 1.0f);
    if (!std::isfinite(config.lastStableWindowSeconds) || config.lastStableWindowSeconds < 0.0)
        config.lastStableWindowSeconds = 1.0;
    // Config changes (mode/rates) never wipe hysteresis/Last Stable state on their own -- e.g.
    // re-applying a persisted profile must not restart hysteresis or cause a visible jump.
    config_ = config;
}

void AdaptiveExposureController::Reset() {
    activeSource_ = ExposureSource::Manual;
    gameStreak_ = SourceStreak {};
    bufferStreak_ = SourceStreak {};
    lastStable_.reset();
    lastStableAgeSeconds_ = 0.0;
}

ExposureDecision AdaptiveExposureController::BuildDecision(ExposureSource source, float whitePoint,
                                                             float confidence, bool stable,
                                                             bool sameFrame,
                                                             std::string fallbackReason) const {
    ExposureDecision d;
    d.source = source;
    d.whitePoint = whitePoint;
    d.confidence = std::isfinite(confidence) ? std::clamp(confidence, 0.0f, 1.0f) : 0.0f;
    d.stable = stable;
    d.sameFrame = sameFrame;
    d.fallbackReason = std::move(fallbackReason);
    return d;
}

ExposureDecision AdaptiveExposureController::Update(const std::optional<ExposureMeasurement>& gameExposure,
                                                      const std::optional<ExposureMeasurement>& bufferScan,
                                                      bool cameraCut, double dtSeconds) {
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0)
        dtSeconds = kDefaultDt;

    const bool gameConfident = MeasurementConfident(gameExposure, config_.minSourceConfidence);
    const bool bufferConfident = MeasurementConfident(bufferScan, config_.minSourceConfidence);

    gameStreak_.Observe(gameConfident);
    bufferStreak_.Observe(bufferConfident);

    if (cameraCut) {
        // Buffer Scan's temporal correlation belongs to the old scene; force it to requalify
        // rather than trusting a streak accumulated against a scene that no longer exists.
        bufferStreak_.streak = bufferConfident ? 1 : -1;
    }

    ExposureSource resolvedSource = ExposureSource::Manual;
    std::string fallbackReason;

    if (config_.mode == ExposureSource::Manual) {
        resolvedSource = ExposureSource::Manual;
    } else if (config_.mode == ExposureSource::GameExposure) {
        if (gameConfident)
            resolvedSource = ExposureSource::GameExposure;
        else
            fallbackReason = "Game Exposure forced but unavailable";
    } else if (config_.mode == ExposureSource::BufferScan) {
        if (bufferConfident)
            resolvedSource = ExposureSource::BufferScan;
        else
            fallbackReason = "Buffer Scan forced but unavailable";
    } else {
        // Auto: Game Exposure > Buffer Scan > Last Stable > Manual. Promotion requires sustained
        // confidence (or a camera cut for Game Exposure specifically); the currently active
        // source may keep running through a dip until it fails to recover for dropSustainFrames.
        if (cameraCut && gameConfident) {
            resolvedSource = ExposureSource::GameExposure;
        } else if (gameConfident && gameStreak_.streak >= config_.promoteSustainFrames) {
            resolvedSource = ExposureSource::GameExposure;
        } else if (bufferConfident && bufferStreak_.streak >= config_.promoteSustainFrames) {
            resolvedSource = ExposureSource::BufferScan;
        } else if (activeSource_ == ExposureSource::GameExposure &&
                   gameStreak_.streak > -config_.dropSustainFrames) {
            resolvedSource = ExposureSource::GameExposure;
        } else if (activeSource_ == ExposureSource::BufferScan &&
                   bufferStreak_.streak > -config_.dropSustainFrames) {
            resolvedSource = ExposureSource::BufferScan;
        } else {
            resolvedSource = ExposureSource::Manual;
            fallbackReason = "no confident automatic source";
        }
    }

    ExposureDecision decision;

    // Hysteresis may sustain a source through a dip using only its streak, not this frame's
    // actual data -- a "sustain through dip" resolution with a present-but-weak measurement still
    // has real numbers to report, but if the measurement is altogether absent (nullopt) there is
    // nothing to read here. Fall through to Manual/Last Stable instead of dereferencing nothing.
    if (resolvedSource == ExposureSource::GameExposure && gameExposure.has_value()) {
        const auto& m = *gameExposure;
        decision = BuildDecision(ExposureSource::GameExposure, m.whitePoint, m.confidence, true,
                                  m.sameFrame, "");
        lastStable_ = decision;
        lastStableAgeSeconds_ = 0.0;
    } else if (resolvedSource == ExposureSource::BufferScan && bufferScan.has_value()) {
        const auto& m = *bufferScan;
        decision = BuildDecision(ExposureSource::BufferScan, m.whitePoint, m.confidence, true,
                                  false, "");
        lastStable_ = decision;
        lastStableAgeSeconds_ = 0.0;
    } else if (resolvedSource == ExposureSource::GameExposure || resolvedSource == ExposureSource::BufferScan) {
        resolvedSource = ExposureSource::Manual;
        fallbackReason = "sustained source has no measurement this frame";
    }

    if (resolvedSource == ExposureSource::Manual) {
        const bool lastStableEligible = config_.mode != ExposureSource::Manual && lastStable_.has_value() &&
                                         lastStableAgeSeconds_ <= config_.lastStableWindowSeconds;
        if (lastStableEligible) {
            decision = *lastStable_;
            decision.stable = false; // reused, not a fresh confident reading this frame.
            decision.sameFrame = false;
            decision.fallbackReason = fallbackReason.empty() ? "using Last Stable" : fallbackReason + "; using Last Stable";
            lastStableAgeSeconds_ += dtSeconds;
        } else {
            // Genuine Manual (explicit mode, or the automatic chain exhausted): the manual white
            // point itself is always a trustworthy, intentional value -- the host substitutes its
            // own DlssNrWhitePointScale here, this controller does not own or see that number.
            decision = BuildDecision(ExposureSource::Manual, 0.0f, 0.0f, true, false,
                                      fallbackReason.empty() ? "manual white point" : fallbackReason + "; manual white point");
        }
    }

    activeSource_ = resolvedSource;
    return decision;
}

} // namespace nrfusion
