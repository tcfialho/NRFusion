#pragma once

#include "nrfusion/Types.hpp"

#include <cstdint>
#include <vector>

namespace nrfusion {

struct ResidualImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> residual;
    std::vector<float> depth;
    float preExposure = 0.0f;
    FrameId sourceFrame = 0;
};

struct ResidualReprojectionInput {
    ResidualImage current;
    ResidualImage history;
    std::vector<float> motionX; // pixels, current -> previous: history coordinate = current - motion
    std::vector<float> motionY;
    std::vector<float> confidence; // 0..1
    bool cameraCut = false;
};

struct ResidualReprojectionConfig {
    float currentBlend = 0.08f;
    float minConfidence = 0.25f;
    float depthRelativeThreshold = 0.04f;
    float maxMotionPixels = 512.0f;
};

struct ResidualReprojectionOutput {
    ResidualImage image;
    std::vector<std::uint8_t> historyAccepted;
};

class ResidualReprojection {
public:
    explicit ResidualReprojection(ResidualReprojectionConfig config = {});
    ResidualReprojectionOutput Run(const ResidualReprojectionInput& input) const;

private:
    ResidualReprojectionConfig config_;
};

} // namespace nrfusion
