#include "nrfusion/VulkanCarrierAcquire.hpp"

#include <cmath>
#include <iterator>

namespace nrfusion {
namespace {

bool ValidImage(
    const VulkanNativeImageFacts& image,
    std::uint32_t requiredUsageAny) noexcept {
    return image.opaqueId != 0 &&
           image.image2D &&
           image.resolution.Valid() &&
           image.format != ResourceFormat::Unknown &&
           image.usage != 0 &&
           (requiredUsageAny == 0 ||
            (image.usage & requiredUsageAny) != 0) &&
           image.layout != VulkanImageLayoutIntent::Undefined &&
           image.queueFamilyIndex !=
               std::numeric_limits<std::uint32_t>::max() &&
           image.mipLevels == 1 &&
           image.arrayLayers == 1 &&
           image.sampleCount == 1;
}

bool HasEvidence(const VulkanNativeResourceCapture& capture) noexcept {
    return capture.provenance != ResourceProvenance::Unknown &&
           capture.reliability != ResourceReliability::Unknown;
}

ResourceRef ToResource(
    const VulkanNativeResourceCapture& capture,
    FrameId frameId) noexcept {
    ResourceRef resource{};
    if (capture.image.opaqueId == 0) return resource;

    resource.opaqueId = capture.image.opaqueId;
    resource.resolution = capture.image.resolution;
    resource.format = capture.image.format;
    resource.provenance = capture.provenance;
    resource.reliability = capture.reliability;
    resource.ownership = ResourceOwnership::Borrowed;
    resource.lifetime = ResourceLifetime::Frame;
    resource.sourceFrameId = frameId;
    return resource;
}

VulkanAcquireFailure ValidateCapture(
    const VulkanNativeResourceCapture& capture,
    bool required,
    std::uint32_t requiredUsageAny) noexcept {
    if (capture.image.opaqueId == 0)
        return required
            ? VulkanAcquireFailure::MissingColor
            : VulkanAcquireFailure::None;
    if (!ValidImage(capture.image, requiredUsageAny)) {
        if (capture.image.usage == 0 ||
            (capture.image.usage & requiredUsageAny) == 0)
            return VulkanAcquireFailure::InvalidUsage;
        if (capture.image.layout == VulkanImageLayoutIntent::Undefined)
            return VulkanAcquireFailure::InvalidLayout;
        if (capture.image.queueFamilyIndex ==
            std::numeric_limits<std::uint32_t>::max())
            return VulkanAcquireFailure::InvalidQueueFamily;
        return VulkanAcquireFailure::InvalidImage;
    }
    if (!HasEvidence(capture))
        return VulkanAcquireFailure::InvalidEvidence;
    return VulkanAcquireFailure::None;
}

bool TrustedOptionalResource(
    const ResourceRef& resource, FrameId frameId) noexcept {
    if (!resource.Valid()) return true;
    return resource.EvidenceWellFormed() &&
           resource.BelongsToFrame(frameId) &&
           resource.reliability != ResourceReliability::Unreliable;
}

} // namespace

VulkanAcquireResult AcquireVulkanFrame(
    const VulkanNativeAcquireInput& input) noexcept {
    VulkanAcquireResult result{};
    result.attempted = true;

    if (input.identity.frameId == 0 ||
        input.identity.configurationGeneration == 0) {
        result.failure = VulkanAcquireFailure::InvalidIdentity;
        return result;
    }
    if (input.resourceGeneration == 0) {
        result.failure = VulkanAcquireFailure::InvalidResourceGeneration;
        return result;
    }

    const auto colorFailure = ValidateCapture(
        input.color, true,
        VulkanUsageSampled | VulkanUsageTransferSource);
    if (colorFailure != VulkanAcquireFailure::None) {
        result.failure = colorFailure;
        return result;
    }
    if (input.output.opaqueId == 0) {
        result.failure = VulkanAcquireFailure::MissingOutput;
        return result;
    }
    if (!ValidImage(
            input.output,
            VulkanUsageStorage | VulkanUsageTransferDestination)) {
        if (input.output.usage == 0 ||
            (input.output.usage &
             (VulkanUsageStorage |
              VulkanUsageTransferDestination)) == 0)
            result.failure = VulkanAcquireFailure::InvalidUsage;
        else if (input.output.layout == VulkanImageLayoutIntent::Undefined)
            result.failure = VulkanAcquireFailure::InvalidLayout;
        else if (input.output.queueFamilyIndex ==
                 std::numeric_limits<std::uint32_t>::max())
            result.failure = VulkanAcquireFailure::InvalidQueueFamily;
        else
            result.failure = VulkanAcquireFailure::InvalidImage;
        return result;
    }
    if (input.color.image.queueFamilyIndex !=
        input.output.queueFamilyIndex) {
        result.failure = VulkanAcquireFailure::InvalidQueueFamily;
        return result;
    }

    const VulkanNativeResourceCapture* optional[] = {
        &input.depth,
        &input.motionVectors,
        &input.exposure,
        &input.reactiveMask
    };
    for (const auto* capture : optional) {
        const auto failure = ValidateCapture(
            *capture, false, VulkanUsageSampled);
        if (failure != VulkanAcquireFailure::None) {
            result.failure = failure;
            return result;
        }
    }

    if (input.color.image.resolution != input.output.resolution) {
        result.failure = VulkanAcquireFailure::ColorResolutionMismatch;
        return result;
    }

    FrameContext& frame = result.frame;
    frame.frameId = input.identity.frameId;
    frame.hostFrameToken = input.identity.hostFrameToken;
    frame.viewId = input.identity.viewId;
    frame.configurationGeneration =
        input.identity.configurationGeneration;
    frame.renderResolution = input.color.image.resolution;
    frame.outputResolution = input.output.resolution;
    frame.jitter = input.jitter;
    frame.color = ToResource(input.color, input.identity.frameId);
    frame.depth = ToResource(input.depth, input.identity.frameId);
    frame.motionVectors =
        ToResource(input.motionVectors, input.identity.frameId);
    frame.exposure = ToResource(input.exposure, input.identity.frameId);
    frame.reactiveMask =
        ToResource(input.reactiveMask, input.identity.frameId);
    frame.hdr = input.hdr;
    frame.cameraCut = input.cameraCut;
    frame.resetHistory = input.resetHistory;
    frame.api = GraphicsApi::Vulkan;
    frame.depthReliable =
        frame.depth.reliability == ResourceReliability::Reliable;
    frame.motionVectorSource = frame.EffectiveMotionSource();
    frame.motionVectorsReliable =
        frame.motionVectors.reliability ==
        ResourceReliability::Reliable;

    if (!frame.ReadyForCore()) {
        result.failure = VulkanAcquireFailure::InvalidEvidence;
        return result;
    }

    result.outputOpaqueId = input.output.opaqueId;
    result.resourceGeneration = input.resourceGeneration;
    result.colorFacts = input.color.image;
    result.outputFacts = input.output;
    result.colorLayout = input.color.image.layout;
    result.outputLayout = input.output.layout;
    result.queueFamilyIndex = input.output.queueFamilyIndex;
    result.diagnostics.supported = true;
    result.diagnostics.frameComplete = true;
    result.diagnostics.depthValid = frame.DepthReliable();
    result.diagnostics.motionValid =
        frame.MotionReliable(frame.EffectiveMotionSource());
    result.diagnostics.exposureValid = frame.ExposureReliable();
    return result;
}

std::optional<SyntheticFrameInputs> BuildVulkanCarrierWork(
    const FrameContext& frame, const WorkTicket& ticket) noexcept {
    if (frame.api != GraphicsApi::Vulkan ||
        !frame.ReadyForCore() ||
        ticket.id == 0 ||
        ticket.session == 0 ||
        ticket.sourceFrame != frame.frameId ||
        ticket.viewKey != frame.viewId ||
        ticket.configurationGeneration !=
            frame.configurationGeneration ||
        !std::isfinite(ticket.workingScale) ||
        ticket.workingScale <= 0.0f ||
        ticket.workingScale > 1.0f)
        return std::nullopt;

    if (frame.color.provenance != ResourceProvenance::GameNative ||
        frame.color.reliability != ResourceReliability::Reliable ||
        !TrustedOptionalResource(frame.depth, frame.frameId) ||
        !TrustedOptionalResource(frame.motionVectors, frame.frameId) ||
        !TrustedOptionalResource(frame.exposure, frame.frameId) ||
        !TrustedOptionalResource(frame.reactiveMask, frame.frameId))
        return std::nullopt;

    SyntheticFrameInputs inputs{};
    inputs.ticket = ticket;
    inputs.frameId = frame.frameId;
    inputs.color = frame.color;
    inputs.depth = frame.depth;
    inputs.motionVectors = frame.motionVectors;
    inputs.exposure = frame.exposure;
    inputs.reactiveMask = frame.reactiveMask;
    inputs.renderResolution = frame.renderResolution;
    inputs.targetResolution = frame.outputResolution;
    inputs.jitter = frame.jitter;
    inputs.workingScale = ticket.workingScale;
    inputs.reset = frame.resetHistory;
    inputs.hdr = frame.hdr;
    inputs.cameraCut = frame.cameraCut;
    inputs.motionSource = frame.EffectiveMotionSource();
    return inputs;
}

} // namespace nrfusion
