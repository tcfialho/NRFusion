#pragma once

#include "nrfusion/Types.hpp"

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
    // Resolves motion vector according to the strict priority hierarchy:
    // 1. Native engine MV
    // 2. Existing DLSS contract MV
    // 3. Shader / ReShade temporal MV
    // 4. Low-res async NVOF
    // 5. Zero / static fallback
    static MotionResolutionResult Resolve(const MotionCandidates& candidates,
                                          Resolution fullResolution) noexcept {
        MotionResolutionResult result{};

        // If camera cut occurred, force static zero vectors
        if (candidates.cameraCut) {
            result.category = ResolvedMotionCategory::ZeroFallback;
            result.source = MotionSource::Zero;
            result.scaleX = 1.0f;
            result.scaleY = 1.0f;
            result.isReliable = true;
            result.requiresNvofCompute = false;
            return result;
        }

        // 1. Native Engine MV
        if (candidates.nativeEngineMv.Valid() && candidates.nativeReliable) {
            result.category = ResolvedMotionCategory::NativeEngine;
            result.source = MotionSource::Native;
            result.motionVectors = candidates.nativeEngineMv;
            result.isReliable = true;
            result.requiresNvofCompute = false;
            CalculateScale(candidates.nativeEngineMv.resolution, fullResolution, result.scaleX, result.scaleY);
            return result;
        }

        // 2. Existing DLSS Contract MV
        if (candidates.dlssContractMv.Valid() && candidates.contractReliable) {
            result.category = ResolvedMotionCategory::DlssContract;
            result.source = MotionSource::DlssContract;
            result.motionVectors = candidates.dlssContractMv;
            result.isReliable = true;
            result.requiresNvofCompute = false;
            CalculateScale(candidates.dlssContractMv.resolution, fullResolution, result.scaleX, result.scaleY);
            return result;
        }

        // 3. Shader / Temporal MV
        if (candidates.shaderEstimatedMv.Valid() && candidates.shaderReliable) {
            result.category = ResolvedMotionCategory::ShaderEstimated;
            result.source = MotionSource::ShaderEstimated;
            result.motionVectors = candidates.shaderEstimatedMv;
            result.isReliable = true;
            result.requiresNvofCompute = false;
            CalculateScale(candidates.shaderEstimatedMv.resolution, fullResolution, result.scaleX, result.scaleY);
            return result;
        }

        // 4. Low-Res Async NVOF
        if (candidates.nvofHardwareAvailable) {
            result.category = ResolvedMotionCategory::NvidiaOpticalFlow;
            result.source = MotionSource::NvidiaOpticalFlow;
            result.isReliable = true;
            result.requiresNvofCompute = true;
            return result;
        }

        // 5. Zero Fallback
        result.category = ResolvedMotionCategory::ZeroFallback;
        result.source = MotionSource::Zero;
        result.isReliable = false;
        result.requiresNvofCompute = false;
        result.scaleX = 1.0f;
        result.scaleY = 1.0f;
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
