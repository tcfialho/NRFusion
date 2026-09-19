#pragma once

#include <cstdint>
#include <vector>

namespace nrfusion {

struct MotionConfidenceInput {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Canonical current -> previous displacement, in current-domain pixels.
    std::vector<float> forwardX;
    std::vector<float> forwardY;
    // Previous -> current field sampled at the reprojected previous-frame coordinate.
    std::vector<float> backwardX;
    std::vector<float> backwardY;
    std::vector<float> currentDepth;
    std::vector<float> historyDepth;
    bool cameraCut = false;
};

struct MotionConfidenceConfig {
    float maxConsistencyErrorPixels = 2.0f;
    float depthRelativeThreshold = 0.04f;
    float maxMotionPixels = 512.0f;
};

struct MotionConfidenceResult {
    std::vector<float> confidence;
    std::vector<std::uint8_t> valid;
    double meanConfidence = 0.0;
    double validPixelRatio = 0.0;
    double forwardBackwardAgreement = 0.0;
    double depthAgreement = 0.0;
    bool validShape = false;
};

// CPU reference for provider/shader implementations. It combines forward/backward flow consistency
// with historical depth compatibility and fails closed on camera cuts, malformed fields, NaN/Inf or
// reprojection outside the previous frame.
class MotionConfidenceEngine {
public:
    explicit MotionConfidenceEngine(MotionConfidenceConfig config = {});
    MotionConfidenceResult Evaluate(const MotionConfidenceInput& input) const;

private:
    MotionConfidenceConfig config_;
};

} // namespace nrfusion
