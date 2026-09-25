#pragma once

#include "nrfusion/Types.hpp"
#include "nrfusion/MotionGuideSelection.hpp"

#include <cstdint>

namespace nrfusion {

enum class ResolvedMotionCategory : uint8_t {
    NativeEngine,
    DlssContract,
    ShaderEstimated,
    NvidiaOpticalFlow,
    ZeroFallback
};

struct MotionResolutionResult {
    ResolvedMotionCategory category = ResolvedMotionCategory::ZeroFallback;
    MotionSource source = MotionSource::Zero;
    ResourceRef motionVectors{};
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    bool isReliable = false;
    bool requiresNvofCompute = false;
};

struct MotionCandidates {
    ResourceRef nativeEngineMv{};
    bool nativeReliable = false;

    ResourceRef dlssContractMv{};
    bool contractReliable = false;

    ResourceRef shaderEstimatedMv{};
    bool shaderReliable = false;

    bool nvofHardwareAvailable = false;
    bool cameraCut = false;
};

class MotionVectorResolver {
public:
    static MotionResolutionResult Resolve(
        const MotionCandidates& candidates,
        Resolution fullResolution) noexcept {
        MotionGuideAvailability guides{};
        guides.nativeReliable =
            candidates.nativeEngineMv.Valid() &&
            candidates.nativeReliable;
        guides.dlssContractReliable =
            candidates.dlssContractMv.Valid() &&
            candidates.contractReliable;
        guides.nvofAvailable =
            candidates.nvofHardwareAvailable;
        guides.shaderReliable =
            candidates.shaderEstimatedMv.Valid() &&
            candidates.shaderReliable;
        guides.cameraCut = candidates.cameraCut;

        MotionResolutionResult result{};
        result.source = SelectMotionGuide(guides);
        switch (result.source) {
        case MotionSource::Native:
            result.category =
                ResolvedMotionCategory::NativeEngine;
            result.motionVectors =
                candidates.nativeEngineMv;
            result.isReliable = true;
            CalculateScale(
                result.motionVectors.resolution,
                fullResolution,
                result.scaleX,
                result.scaleY);
            break;
        case MotionSource::DlssContract:
            result.category =
                ResolvedMotionCategory::DlssContract;
            result.motionVectors =
                candidates.dlssContractMv;
            result.isReliable = true;
            CalculateScale(
                result.motionVectors.resolution,
                fullResolution,
                result.scaleX,
                result.scaleY);
            break;
        case MotionSource::NvidiaOpticalFlow:
            result.category =
                ResolvedMotionCategory::NvidiaOpticalFlow;
            result.isReliable = true;
            result.requiresNvofCompute = true;
            break;
        case MotionSource::ShaderEstimated:
            result.category =
                ResolvedMotionCategory::ShaderEstimated;
            result.motionVectors =
                candidates.shaderEstimatedMv;
            result.isReliable = true;
            CalculateScale(
                result.motionVectors.resolution,
                fullResolution,
                result.scaleX,
                result.scaleY);
            break;
        case MotionSource::Zero:
            result.category =
                ResolvedMotionCategory::ZeroFallback;
            result.scaleX = 1.0f;
            result.scaleY = 1.0f;
            result.isReliable = candidates.cameraCut;
            break;
        }
        return result;
    }

private:
    static void CalculateScale(Resolution mvRes, Resolution fullRes, float& outX, float& outY) noexcept {
        if (mvRes.Valid() && fullRes.Valid()) {
            outX = static_cast<float>(fullRes.width) / static_cast<float>(mvRes.width);
            outY = static_cast<float>(fullRes.height) / static_cast<float>(mvRes.height);
        } else {
            outX = 1.0f;
            outY = 1.0f;
        }
    }
};

} // namespace nrfusion
