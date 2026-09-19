#pragma once
#include "nrfusion/Types.hpp"

namespace nrfusion {

struct ResidualInputs {
    bool rayReconstruction = false;
    bool cameraCut = false;
    bool hasDepth = false;
    double motionConfidence = 0.0;
    double disocclusionRatio = 1.0;
};

struct ResidualDecision {
    bool useResidual = false;
    bool accumulateHistory = false;
    bool resetHistory = false;
    double historyWeight = 0.0;
};

class ResidualPolicy {
public:
    ResidualDecision Decide(const ResidualInputs& input) const;
};

} // namespace nrfusion
