#pragma once

#include <cstdint>

namespace nrfusion {

struct TemporalConfidenceInput {
    bool cameraCut = false;
    bool hasDepth = false;
    double motionConfidence = 0.0;      // confidence reported by native MV/NVOF provider
    double forwardBackwardAgreement = 0.0; // NVOF/native consistency, 0..1
    double depthAgreement = 0.0;        // reprojected-vs-current depth agreement, 0..1
    double disocclusionRatio = 1.0;     // 0..1
    double motionMagnitudePixels = 0.0;
};

struct TemporalConfidenceResult {
    double confidence = 0.0;
    bool rejectHistory = true;
    bool hardReset = false;
};

class TemporalConfidence {
public:
    TemporalConfidenceResult Evaluate(const TemporalConfidenceInput& input) const;
};

} // namespace nrfusion
