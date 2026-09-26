#include "nrfusion/MotionGuideBinding.hpp"

namespace nrfusion {
namespace {

void ClearMotion(FrameContext& frame) noexcept {
    frame.motionVectors = {};
    frame.motionVectorSource = MotionSource::Zero;
    frame.motionVectorsReliable = false;
}

bool EvidenceConflicts(
    ResourceReliability existingReliability,
    ResourceOwnership existingOwnership,
    ResourceLifetime existingLifetime,
    const MotionGuideBinding& binding) noexcept {
    return (existingReliability != ResourceReliability::Unknown &&
            existingReliability != binding.reliability) ||
           (existingOwnership != ResourceOwnership::Unknown &&
            existingOwnership != binding.ownership) ||
           (existingLifetime != ResourceLifetime::Unknown &&
            existingLifetime != binding.lifetime);
}

} // namespace

MotionGuideBindingResult BindMotionGuide(
    FrameContext& frame,
    const MotionGuideBinding& binding) noexcept {
    auto fail = [&](MotionGuideBindingFailure failure) noexcept {
        ClearMotion(frame);
        return MotionGuideBindingResult{failure};
    };

    if (frame.frameId == 0)
        return fail(MotionGuideBindingFailure::InvalidFrame);
    if (binding.configurationGeneration !=
        frame.configurationGeneration) {
        return fail(
            MotionGuideBindingFailure::ConfigurationGenerationMismatch);
    }

    if (binding.source == MotionSource::Zero &&
        !binding.resource.Valid()) {
        if (binding.sourceFrameId != 0 ||
            binding.reliability != ResourceReliability::Unknown ||
            binding.ownership != ResourceOwnership::Unknown ||
            binding.lifetime != ResourceLifetime::Unknown) {
            return fail(
                MotionGuideBindingFailure::InvalidZeroBinding);
        }
        ClearMotion(frame);
        return {};
    }

    if (!binding.resource.Valid())
        return fail(MotionGuideBindingFailure::MissingResource);
    if (!MotionGuideFormatSupported(binding.resource.format))
        return fail(MotionGuideBindingFailure::UnsupportedFormat);
    if (binding.reliability == ResourceReliability::Unknown ||
        binding.ownership == ResourceOwnership::Unknown ||
        binding.lifetime == ResourceLifetime::Unknown) {
        return fail(MotionGuideBindingFailure::MissingEvidence);
    }
    if (EvidenceConflicts(
            binding.resource.reliability,
            binding.resource.ownership,
            binding.resource.lifetime,
            binding)) {
        return fail(MotionGuideBindingFailure::EvidenceMismatch);
    }
    if (binding.sourceFrameId == 0 ||
        binding.sourceFrameId != frame.frameId) {
        return fail(MotionGuideBindingFailure::SourceFrameMismatch);
    }

    const ResourceProvenance expected =
        MotionGuideProvenance(binding.source);
    if (binding.resource.provenance != ResourceProvenance::Unknown &&
        binding.resource.provenance != expected) {
        return fail(MotionGuideBindingFailure::ProvenanceMismatch);
    }
    if (binding.resource.sourceFrameId != 0 &&
        binding.resource.sourceFrameId != binding.sourceFrameId) {
        return fail(MotionGuideBindingFailure::SourceFrameMismatch);
    }

    ResourceRef resource = binding.resource;
    resource.provenance = expected;
    resource.reliability = binding.reliability;
    resource.ownership = binding.ownership;
    resource.lifetime = binding.lifetime;
    resource.sourceFrameId = binding.sourceFrameId;

    frame.motionVectors = resource;
    frame.motionVectorSource = binding.source;
    frame.motionVectorsReliable =
        binding.reliability == ResourceReliability::Reliable;
    return {};
}

} // namespace nrfusion
