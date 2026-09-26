#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>

namespace nrfusion {

enum class MotionGuideBindingFailure : std::uint8_t {
    None,
    InvalidFrame,
    InvalidZeroBinding,
    MissingResource,
    UnsupportedFormat,
    MissingEvidence,
    EvidenceMismatch,
    SourceFrameMismatch,
    ConfigurationGenerationMismatch,
    ProvenanceMismatch
};

struct MotionGuideBinding {
    MotionSource source = MotionSource::Zero;
    ResourceRef resource{};
    ResourceReliability reliability =
        ResourceReliability::Unknown;
    ResourceOwnership ownership =
        ResourceOwnership::Unknown;
    ResourceLifetime lifetime =
        ResourceLifetime::Unknown;
    FrameId sourceFrameId = 0;
    std::uint64_t configurationGeneration = 0;
};

struct MotionGuideBindingResult {
    MotionGuideBindingFailure failure =
        MotionGuideBindingFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == MotionGuideBindingFailure::None;
    }
};

constexpr ResourceProvenance MotionGuideProvenance(
    MotionSource source) noexcept {
    switch (source) {
    case MotionSource::Native:
        return ResourceProvenance::GameNative;
    case MotionSource::DlssContract:
        return ResourceProvenance::DlssContract;
    case MotionSource::NvidiaOpticalFlow:
        return ResourceProvenance::OpticalFlow;
    case MotionSource::ShaderEstimated:
        return ResourceProvenance::ShaderEstimated;
    case MotionSource::Zero:
        return ResourceProvenance::Generated;
    }
    return ResourceProvenance::Unknown;
}

constexpr bool MotionGuideFormatSupported(
    ResourceFormat format) noexcept {
    return format == ResourceFormat::Rg16Float ||
           format == ResourceFormat::Rg32Float;
}

MotionGuideBindingResult BindMotionGuide(
    FrameContext& frame,
    const MotionGuideBinding& binding) noexcept;

} // namespace nrfusion
