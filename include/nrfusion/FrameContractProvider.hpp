#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

struct ProviderInput {
    FrameId frameId = 0;
    std::uint64_t hostFrameToken = 0;
    std::uint64_t viewId = 0;
    std::uint64_t configurationGeneration = 0;
};

struct ProviderDiagnostics {
    bool supported = false;
    bool frameComplete = false;
    bool depthValid = false;
    bool motionValid = false;
    bool exposureValid = false;
};

class IFrameContractProvider {
public:
    virtual ~IFrameContractProvider() = default;
    virtual bool IsSupported(const GameContext& game) const = 0;
    virtual FrameContext AcquireFrame(const ProviderInput& input) = 0;
    virtual ProviderDiagnostics Diagnostics() const = 0;
};

} // namespace nrfusion
