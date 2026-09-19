#pragma once

#include <cstdint>

namespace nrfusion {

struct MotionGuideSample {
    bool present = false;
    bool cameraCut = false;
    double validPixelRatio = 0.0;      // finite/in-range vectors
    double temporalAgreement = 0.0;    // agreement with previous accepted field
    double depthAgreement = 0.0;       // optional depth-consistency score
    double magnitudeSanity = 0.0;      // rejects globally absurd vector magnitude
};

struct MotionGuideResult {
    bool reliable = false;
    double confidence = 0.0;
    std::uint32_t goodFrames = 0;
    std::uint32_t badFrames = 0;
};

struct MotionGuideValidationConfig {
    double enterReliable = 0.72;
    double exitReliable = 0.45;
    std::uint32_t goodFramesRequired = 2;
    std::uint32_t badFramesRequired = 2;
    double hardValidMinimum = 0.25;
    double hardTemporalMinimum = 0.15;
    double hardDepthMinimum = 0.20;
    double hardMagnitudeMinimum = 0.20;
};

// Whole-field hysteresis used before motion-source selection. It prevents one noisy frame from flipping
// Native/DLSS motion to NVOF and back on every present.
class MotionGuideValidator {
public:
    explicit MotionGuideValidator(MotionGuideValidationConfig config = {});
    MotionGuideResult Update(const MotionGuideSample& sample);
    void Reset();

private:
    MotionGuideValidationConfig config_;
    bool reliable_ = false;
    std::uint32_t goodFrames_ = 0;
    std::uint32_t badFrames_ = 0;
};

struct StaticMotionDecision {
    bool zeroMotion = false;
    bool maskHistory = false;
};

// Pixel-level rule to mirror in the synthetic-guide shader: zero a vector only after the
// static hypothesis wins on two consecutive frames. First win keeps the vector but masks history.
StaticMotionDecision ResolveStaticMotion(bool staticWinsNow, bool staticWonPreviousFrame);

} // namespace nrfusion
