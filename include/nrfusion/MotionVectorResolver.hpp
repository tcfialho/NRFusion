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

    bool nvofGuideAvailable = false;
    bool cameraCut = false;
    bool resetHistory = false;
    FrameId frameId = 0;
};

class MotionVectorResolver {
public:
    static MotionResolutionResult Resolve(
        const MotionCandidates& candidates,
        Resolution fullResolution) noexcept {
        MotionGuideAvailability guides{};
        guides.nativeReliable = CandidateReliable(
            candidates.nativeEngineMv,
            ResourceProvenance::GameNative,
            candidates.nativeReliable,
            candidates.frameId);
        guides.dlssContractReliable = CandidateReliable(
            candidates.dlssContractMv,
            ResourceProvenance::DlssContract,
            candidates.contractReliable,
            candidates.frameId);
        guides.nvofAvailable =
            candidates.nvofGuideAvailable;
        guides.shaderReliable = CandidateReliable(
            candidates.shaderEstimatedMv,
            ResourceProvenance::ShaderEstimated,
            candidates.shaderReliable,
            candidates.frameId);
        guides.cameraCut = candidates.cameraCut;
        guides.resetHistory = candidates.resetHistory;

        MotionResolutionResult result{};
        result.source = SelectMotionGuide(guides);
        switch (result.source) {
        case MotionSource::Native:
            SetResourceResult(
                result,
                ResolvedMotionCategory::NativeEngine,
                candidates.nativeEngineMv,
                fullResolution);
            break;
        case MotionSource::DlssContract:
            SetResourceResult(
                result,
                ResolvedMotionCategory::DlssContract,
                candidates.dlssContractMv,
                fullResolution);
            break;
        case MotionSource::NvidiaOpticalFlow:
            result.category =
                ResolvedMotionCategory::NvidiaOpticalFlow;
            result.isReliable = true;
            result.requiresNvofCompute = false;
            break;
        case MotionSource::ShaderEstimated:
            SetResourceResult(
                result,
                ResolvedMotionCategory::ShaderEstimated,
                candidates.shaderEstimatedMv,
                fullResolution);
            break;
        case MotionSource::Zero:
            result.category =
                ResolvedMotionCategory::ZeroFallback;
            result.scaleX = 1.0f;
            result.scaleY = 1.0f;
            result.isReliable =
                candidates.cameraCut ||
                candidates.resetHistory;
            break;
        }
        return result;
    }

private:
    static bool CandidateReliable(
        const ResourceRef& resource,
        ResourceProvenance expected,
        bool declaredReliable,
        FrameId frameId) noexcept {
        if (!resource.Valid() || !declaredReliable)
            return false;
        if (!resource.EvidenceWellFormed())
            return false;
        if (frameId != 0 && !resource.BelongsToFrame(frameId))
            return false;
        if (resource.EvidenceUnspecified())
            return true;
        return resource.provenance == expected &&
               resource.reliability ==
                   ResourceReliability::Reliable;
    }

    static void SetResourceResult(
        MotionResolutionResult& result,
        ResolvedMotionCategory category,
        const ResourceRef& resource,
        Resolution fullResolution) noexcept {
        result.category = category;
        result.motionVectors = resource;
        result.isReliable = true;
        CalculateScale(
            resource.resolution,
            fullResolution,
            result.scaleX,
            result.scaleY);
    }

    static void CalculateScale(
        Resolution mvRes,
        Resolution fullRes,
        float& outX,
        float& outY) noexcept {
        if (mvRes.Valid() && fullRes.Valid()) {
            outX = static_cast<float>(fullRes.width) /
                   static_cast<float>(mvRes.width);
            outY = static_cast<float>(fullRes.height) /
                   static_cast<float>(mvRes.height);
        } else {
            outX = 1.0f;
            outY = 1.0f;
        }
    }
};

} // namespace nrfusion
