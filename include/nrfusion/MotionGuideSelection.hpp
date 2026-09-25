#pragma once

#include "nrfusion/FrameContract.hpp"

namespace nrfusion {

struct MotionGuideAvailability {
    bool nativeReliable = false;
    bool dlssContractReliable = false;
    bool nvofAvailable = false;
    bool shaderReliable = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

constexpr MotionSource SelectMotionGuide(
    const MotionGuideAvailability& guides) noexcept {
    if (guides.cameraCut || guides.resetHistory)
        return MotionSource::Zero;
    if (guides.nativeReliable)
        return MotionSource::Native;
    if (guides.dlssContractReliable)
        return MotionSource::DlssContract;
    if (guides.nvofAvailable)
        return MotionSource::NvidiaOpticalFlow;
    if (guides.shaderReliable)
        return MotionSource::ShaderEstimated;
    return MotionSource::Zero;
}

} // namespace nrfusion
