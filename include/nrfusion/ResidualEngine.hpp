#pragma once
#include "nrfusion/ResidualReprojection.hpp"

#include <cstdint>
#include <vector>

namespace nrfusion {

struct ScalarImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> values;
};

struct ResidualExtractInput {
    ScalarImage original;
    ScalarImage neural;
    std::vector<float> depth;
    float preExposure = 0.0f;
    FrameId frameId = 0;
};

struct ResidualEngineConfig {
    float minPreExposure = 1.0e-4f;
    float maxPreExposure = 1.0e4f;
    float maxExposureRatio = 16.0f;
    ResidualReprojectionConfig reprojection{};
};

struct ResidualEngineOutput {
    ResidualImage image;
    std::vector<std::uint8_t> historyAccepted;
    bool valid = false;
};

// Portable scalar CPU reference for residual math. GPU shaders/backends should match these
// semantics: extract in the current pre-exposed domain, exposure-normalize historical residual,
// reproject/validate it, then compose onto the final base image.
class ResidualEngine {
public:
    explicit ResidualEngine(ResidualEngineConfig config = {});

    ResidualImage Extract(const ResidualExtractInput& input) const;
    ResidualImage NormalizeExposure(const ResidualImage& history, float currentPreExposure) const;
    ResidualEngineOutput Reproject(const ResidualImage& current, const ResidualImage& history,
                                   const std::vector<float>& motionX,
                                   const std::vector<float>& motionY,
                                   const std::vector<float>& confidence,
                                   bool cameraCut) const;
    ScalarImage Compose(const ScalarImage& base, const ResidualImage& residual,
                        float residualWeight = 1.0f) const;

private:
    bool ValidExposure(float value) const noexcept;

    ResidualEngineConfig config_;
    ResidualReprojection reprojection_;
};

} // namespace nrfusion
