#pragma once

#include <cstdint>

namespace nrfusion {

struct MgpuInput {
    bool available = false;
    bool stable = false;
    double primaryNrMs = 0.0;
    double secondaryNrMs = 0.0;
    double uploadGbps = 0.0;
    double downloadGbps = 0.0;
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    double workingScale = 1.0;
    std::uint32_t bytesPerInputPixel = 16;   // color + compact guides estimate
    std::uint32_t bytesPerResidualPixel = 8; // FP16 RGBA residual
    double fixedSyncMs = 0.20;
};

struct MgpuPlan {
    bool useSecondary = false;
    bool residualOnlyReturn = true;
    std::uint32_t workWidth = 0;
    std::uint32_t workHeight = 0;
    double estimatedTransferMs = 0.0;
    double estimatedCriticalMs = 0.0;
    double estimatedGainMs = 0.0;
};

class MgpuPlanner {
public:
    MgpuPlan Plan(const MgpuInput& input) const;
};

} // namespace nrfusion
