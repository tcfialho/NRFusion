#include "nrfusion/VulkanCarrierExecutionPlan.hpp"

namespace nrfusion {
namespace {

bool SourceLayout(VulkanImageLayoutIntent layout) noexcept {
    return layout == VulkanImageLayoutIntent::General ||
           layout == VulkanImageLayoutIntent::TransferSource;
}

bool DestinationLayout(VulkanImageLayoutIntent layout) noexcept {
    return layout == VulkanImageLayoutIntent::General ||
           layout == VulkanImageLayoutIntent::TransferDestination;
}

bool ValidOwnership(
    VulkanQueueOwnership ownership, bool externalInterop) noexcept {
    if (externalInterop) return ownership == VulkanQueueOwnership::External;
    return ownership == VulkanQueueOwnership::Local ||
           ownership == VulkanQueueOwnership::Concurrent;
}

bool SameShape(
    const VulkanNativeImageFacts& left,
    const VulkanNativeImageFacts& right) noexcept {
    return left.mipLevels == right.mipLevels &&
           left.arrayLayers == right.arrayLayers &&
           left.sampleCount == right.sampleCount &&
           left.image2D == right.image2D;
}

} // namespace

VulkanCarrierExecutionPlanResult BuildVulkanCarrierExecutionPlan(
    const VulkanCarrierSession& session,
    const VulkanCarrierFrameResult& frame,
    const VulkanCarrierWork& work,
    const VulkanCarrierExecutionResources& resources) noexcept {
    VulkanCarrierExecutionPlanResult result{};

    if (!frame || !frame.session.decision.supported ||
        !frame.session.decision.pipeline.supported ||
        frame.acquire.frame.api != GraphicsApi::Vulkan ||
        frame.session.decision.pipeline.api != GraphicsApi::Vulkan ||
        frame.acquire.frame.frameId != frame.session.frameId ||
        frame.acquire.frame.configurationGeneration !=
            frame.session.configurationGeneration) {
        result.failure = VulkanCarrierExecutionFailure::InvalidFrame;
        return result;
    }
    if (!work || work.submissionEpoch != work.ticket.id) {
        result.failure = VulkanCarrierExecutionFailure::InvalidWork;
        return result;
    }
    if (work.ticket.sourceFrame != frame.session.frameId ||
        work.ticket.viewKey != frame.acquire.frame.viewId) {
        result.failure = VulkanCarrierExecutionFailure::WorkFrameMismatch;
        return result;
    }
    if (work.ticket.configurationGeneration != frame.session.runtimeGeneration ||
        work.ticket.workingScale != frame.session.decision.workingScale) {
        result.failure = VulkanCarrierExecutionFailure::WorkGenerationMismatch;
        return result;
    }
    if (!session.HasClaimedExecution(work)) {
        result.failure = VulkanCarrierExecutionFailure::WorkNotClaimed;
        return result;
    }
    if (resources.recreationGeneration == 0) {
        result.failure =
            VulkanCarrierExecutionFailure::InvalidRecreationGeneration;
        return result;
    }
    if (resources.color.image.opaqueId !=
            frame.acquire.colorFacts.opaqueId ||
        resources.output.opaqueId != frame.acquire.outputFacts.opaqueId ||
        resources.color.image.opaqueId != frame.acquire.frame.color.opaqueId ||
        resources.output.opaqueId != frame.acquire.outputOpaqueId) {
        result.failure =
            VulkanCarrierExecutionFailure::ResourceIdentityMismatch;
        return result;
    }
    if (resources.color.image.queueFamilyIndex !=
            frame.acquire.colorFacts.queueFamilyIndex ||
        resources.output.queueFamilyIndex !=
            frame.acquire.outputFacts.queueFamilyIndex ||
        resources.color.image.queueFamilyIndex != frame.acquire.queueFamilyIndex ||
        resources.output.queueFamilyIndex != frame.acquire.queueFamilyIndex) {
        result.failure = VulkanCarrierExecutionFailure::InvalidQueueFamily;
        return result;
    }
    if (resources.color.image.layout != frame.acquire.colorFacts.layout ||
        resources.output.layout != frame.acquire.outputFacts.layout ||
        !SourceLayout(resources.color.image.layout) ||
        !DestinationLayout(resources.output.layout)) {
        result.failure = VulkanCarrierExecutionFailure::InvalidLayout;
        return result;
    }
    if (resources.color.image.usage != frame.acquire.colorFacts.usage ||
        resources.output.usage != frame.acquire.outputFacts.usage ||
        (resources.color.image.usage & VulkanUsageTransferSource) == 0 ||
        (resources.output.usage & VulkanUsageTransferDestination) == 0) {
        result.failure = VulkanCarrierExecutionFailure::InvalidUsage;
        return result;
    }
    if (!SameShape(resources.color.image, frame.acquire.colorFacts) ||
        !SameShape(resources.output, frame.acquire.outputFacts) ||
        !resources.color.image.resolution.Valid() ||
        !resources.output.resolution.Valid() ||
        resources.color.image.resolution != frame.acquire.colorFacts.resolution ||
        resources.output.resolution != frame.acquire.outputFacts.resolution ||
        resources.color.image.resolution != frame.acquire.frame.renderResolution ||
        resources.output.resolution != frame.acquire.frame.outputResolution) {
        result.failure = VulkanCarrierExecutionFailure::InvalidDimensions;
        return result;
    }
    if (resources.color.image.format != frame.acquire.colorFacts.format ||
        resources.output.format != frame.acquire.outputFacts.format ||
        resources.color.image.format == ResourceFormat::Unknown ||
        resources.output.format == ResourceFormat::Unknown ||
        resources.color.image.format != resources.output.format) {
        result.failure = VulkanCarrierExecutionFailure::InvalidFormat;
        return result;
    }
    if (resources.color.provenance != frame.acquire.frame.color.provenance ||
        resources.color.reliability != frame.acquire.frame.color.reliability) {
        result.failure = VulkanCarrierExecutionFailure::InvalidProvenance;
        return result;
    }
    if (!ValidOwnership(resources.colorOwnership, resources.externalInterop) ||
        !ValidOwnership(resources.outputOwnership, resources.externalInterop)) {
        result.failure = VulkanCarrierExecutionFailure::InvalidOwnership;
        return result;
    }
    if (resources.externalInterop &&
        (!ValidateVulkanTimeline(
             resources.producer, VulkanTimelineDirection::Wait) ||
         !ValidateVulkanTimeline(
             resources.consumer, VulkanTimelineDirection::Signal))) {
        result.failure = VulkanCarrierExecutionFailure::InvalidTimeline;
        return result;
    }
    if (resources.compose != VulkanCarrierComposeIntent::CopyOrBlit) {
        result.failure = VulkanCarrierExecutionFailure::InvalidComposeIntent;
        return result;
    }

    auto& plan = result.plan;
    plan.ticket = work.ticket;
    plan.submissionEpoch = work.submissionEpoch;
    plan.recreationGeneration = resources.recreationGeneration;
    plan.colorOpaqueId = resources.color.image.opaqueId;
    plan.outputOpaqueId = resources.output.opaqueId;
    plan.colorResolution = resources.color.image.resolution;
    plan.outputResolution = resources.output.resolution;
    plan.colorFormat = resources.color.image.format;
    plan.outputFormat = resources.output.format;
    plan.colorUsage = resources.color.image.usage;
    plan.outputUsage = resources.output.usage;
    plan.colorInitialLayout = resources.color.image.layout;
    plan.outputInitialLayout = resources.output.layout;
    plan.queueFamilyIndex = frame.acquire.queueFamilyIndex;
    plan.colorOwnership = resources.colorOwnership;
    plan.outputOwnership = resources.outputOwnership;
    plan.producer = resources.producer;
    plan.consumer = resources.consumer;
    plan.compose = resources.compose;
    plan.colorProvenance = resources.color.provenance;
    plan.externalInterop = resources.externalInterop;
    plan.reset = frame.acquire.frame.resetHistory ||
                 frame.acquire.frame.cameraCut;
    return result;
}

} // namespace nrfusion
