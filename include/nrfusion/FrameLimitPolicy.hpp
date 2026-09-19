#pragma once

namespace nrfusion {

struct FrameLimitPlan {
    double sourceCapFps = 0.0;       // 0 = uncapped
    double manualSourceCapFps = 0.0; // manual cap converted into real/source-frame domain
    double generationMultiplier = 1.0;
    bool governorActive = false;
};

class GenerationMultiplierTracker {
public:
    explicit GenerationMultiplierTracker(unsigned confirmationsRequired = 3);
    double Update(double realFrameMs, double presentFrameMs, bool fgActive);
    void Reset();
    double Current() const { return current_; }

private:
    unsigned confirmationsRequired_ = 3;
    double current_ = 1.0;
    double candidate_ = 1.0;
    unsigned candidateCount_ = 0;
};

class FrameLimitPolicy {
public:
    // Estimate displayed frames per real/source frame from the host's real-frame and Present
    // intervals. This handles DLSS MFG 2x/3x/4x without needing a backend-specific multiplier API.
    // If timing is unavailable but FG is active, preserve the upstream-safe 2x fallback.
    static double EstimateGenerationMultiplier(double realFrameMs, double presentFrameMs, bool fgActive);

    // The manual cap is in displayed-FPS domain; NRFusion's governor is already real/source FPS.
    // Convert once, then apply the stricter positive cap.
    static FrameLimitPlan Resolve(double manualCapFps, double governorSourceCapFps,
                                  double generationMultiplier);
    static FrameLimitPlan Resolve(double manualCapFps, double governorSourceCapFps, bool fgActive) {
        return Resolve(manualCapFps, governorSourceCapFps, fgActive ? 2.0 : 1.0);
    }
};

} // namespace nrfusion
