#pragma once

#include "nrfusion/D3D12CarrierSession.hpp"
#include "nrfusion/D3D12NrFramePlan.hpp"

#include <cstdint>

namespace nrfusion {

struct D3D12CarrierExecutionConfig {
    std::uint32_t passes = 1;
    bool unlockPasses = false;
    bool proxyBackend = false;
    bool depthInverted = false;
    bool colourIsLinearHdr = false;
    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
};

enum class D3D12CarrierExecutionFailure : std::uint8_t {
    None,
    InvalidFrame,
    InvalidWork,
    WorkFrameMismatch,
    WorkGenerationMismatch,
    MissingDepth,
    MissingMotion,
    MissingExposure,
    UnsupportedPlacement,
    InvalidMotionScale,
    InvalidPlan
};

struct D3D12CarrierExecutionPlan {
    D3D12NrFramePlanInput framePlan{};
    D3D12NrFramePlan resolvedPlan{};
    std::uint64_t submissionEpoch = 0;
    bool reset = false;
    bool depthInverted = false;
    bool useGameExposure = false;
    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
};

struct D3D12CarrierExecutionPlanResult {
    D3D12CarrierExecutionPlan plan{};
    D3D12CarrierExecutionFailure failure =
        D3D12CarrierExecutionFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D12CarrierExecutionFailure::None;
    }
};

D3D12CarrierExecutionPlanResult BuildD3D12CarrierExecutionPlan(
    const D3D12CarrierFrameResult& frame,
    const D3D12CarrierWork& work,
    const D3D12CarrierExecutionConfig& config = {}) noexcept;

} // namespace nrfusion
