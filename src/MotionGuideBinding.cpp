#include "nrfusion/MotionGuideBinding.hpp"

namespace nrfusion {

MotionGuideBindingResult BindMotionGuide(
    FrameContext& frame,
    const MotionGuideBinding& binding) noexcept {
    if (frame.frameId == 0)
        return {MotionGuideBindingFailure::InvalidFrame};

    if (binding.source == MotionSource::Zero) {
        if (binding.resource.Valid() ||
            binding.sourceFrameId != 0 ||
            binding.reliability != ResourceReliability::Unknown ||
            binding.ownership != ResourceOwnership::Unknown ||
            binding.lifetime != ResourceLifetime::Unknown) {
            return {MotionGuideBindingFailure::InvalidZeroBinding};
        }
        frame.motionVectors = {};
        frame.motionVectorSource = MotionSource::Zero;
        frame.motionVectorsReliable = false;
        return {};
    }

    if (!binding.resource.Valid())
        return {MotionGuideBindingFailure::MissingResource};
    if (binding.reliability == ResourceReliability::Unknown ||
        binding.ownership == ResourceOwnership::Unknown ||
        binding.lifetime == ResourceLifetime::Unknown) {
        return {MotionGuideBindingFailure::MissingEvidence};
    }
    if (binding.sourceFrameId == 0 ||
        binding.sourceFrameId != frame.frameId) {
        return {MotionGuideBindingFailure::SourceFrameMismatch};
    }

    const ResourceProvenance expected =
        MotionGuideProvenance(binding.source);
    if (binding.resource.provenance != ResourceProvenance::Unknown &&
        binding.resource.provenance != expected) {
        return {MotionGuideBindingFailure::ProvenanceMismatch};
    }
    if (binding.resource.sourceFrameId != 0 &&
        binding.resource.sourceFrameId != binding.sourceFrameId) {
        return {MotionGuideBindingFailure::SourceFrameMismatch};
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
