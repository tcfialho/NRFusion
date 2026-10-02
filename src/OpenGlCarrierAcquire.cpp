#include "nrfusion/OpenGlCarrierAcquire.hpp"

#include <cmath>

namespace nrfusion {
namespace {

bool HasEvidence(
    const OpenGlNativeResourceCapture& capture) noexcept {
    return capture.provenance != ResourceProvenance::Unknown &&
           capture.reliability != ResourceReliability::Unknown;
}

ResourceRef ToResource(
    const OpenGlNativeResourceCapture& capture,
    FrameId frameId) noexcept {
    ResourceRef resource{};
    resource.opaqueId = capture.texture.textureId;
    resource.resolution = capture.texture.resolution;
    resource.format = capture.texture.format;
    resource.provenance = capture.provenance;
    resource.reliability = capture.reliability;
    resource.ownership = ResourceOwnership::Borrowed;
    resource.lifetime = ResourceLifetime::Frame;
    resource.sourceFrameId = frameId;
    return resource;
}

} // namespace

OpenGlAcquireResult AcquireOpenGlFrame(
    const OpenGlNativeAcquireInput& input) noexcept {
    OpenGlAcquireResult result{};
    result.attempted = true;

    if (input.identity.frameId == 0 ||
        input.identity.configurationGeneration == 0) {
        result.failure = OpenGlAcquireFailure::InvalidIdentity;
        return result;
    }
    if (input.resourceGeneration == 0) {
        result.failure =
            OpenGlAcquireFailure::InvalidResourceGeneration;
        return result;
    }
    if (input.interop.renderContext == 0) {
        result.failure = OpenGlAcquireFailure::MissingContext;
        return result;
    }
    if (!input.interop.Ready()) {
        result.failure =
            OpenGlAcquireFailure::MissingInteropCapability;
        return result;
    }
    if (!input.color.texture.Valid()) {
        result.failure = OpenGlAcquireFailure::InvalidColor;
        return result;
    }
    if (!input.output.Valid()) {
        result.failure = OpenGlAcquireFailure::InvalidOutput;
        return result;
    }
    if (!HasEvidence(input.color)) {
        result.failure = OpenGlAcquireFailure::InvalidEvidence;
        return result;
    }
    if (input.color.texture.resolution != input.output.resolution) {
        result.failure = OpenGlAcquireFailure::ResolutionMismatch;
        return result;
    }

    FrameContext& frame = result.frame;
    frame.frameId = input.identity.frameId;
    frame.hostFrameToken = input.identity.hostFrameToken;
    frame.viewId = input.identity.viewId;
    frame.configurationGeneration =
        input.identity.configurationGeneration;
    frame.renderResolution = input.color.texture.resolution;
    frame.outputResolution = input.output.resolution;
    frame.jitter = input.jitter;
    frame.color = ToResource(input.color, input.identity.frameId);
    frame.hdr = input.hdr;
    frame.cameraCut = input.cameraCut;
    frame.resetHistory = input.resetHistory;
    frame.api = GraphicsApi::OpenGL;

    if (!frame.ReadyForCore()) {
        result.failure = OpenGlAcquireFailure::InvalidEvidence;
        return result;
    }

    result.interop = input.interop;
    result.colorFacts = input.color.texture;
    result.outputFacts = input.output;
    result.resourceGeneration = input.resourceGeneration;
    result.diagnostics.supported = true;
    result.diagnostics.frameComplete = true;
    return result;
}

std::optional<SyntheticFrameInputs> BuildOpenGlCarrierWork(
    const FrameContext& frame,
    const WorkTicket& ticket) noexcept {
    if (frame.api != GraphicsApi::OpenGL ||
        !frame.ReadyForCore() ||
        ticket.id == 0 ||
        ticket.session == 0 ||
        ticket.sourceFrame != frame.frameId ||
        ticket.viewKey != frame.viewId ||
        ticket.configurationGeneration !=
            frame.configurationGeneration ||
        !std::isfinite(ticket.workingScale) ||
        ticket.workingScale <= 0.0f ||
        ticket.workingScale > 1.0f ||
        frame.color.provenance != ResourceProvenance::GameNative ||
        frame.color.reliability != ResourceReliability::Reliable) {
        return std::nullopt;
    }

    SyntheticFrameInputs inputs{};
    inputs.ticket = ticket;
    inputs.frameId = frame.frameId;
    inputs.color = frame.color;
    inputs.renderResolution = frame.renderResolution;
    inputs.targetResolution = frame.outputResolution;
    inputs.jitter = frame.jitter;
    inputs.workingScale = ticket.workingScale;
    inputs.reset = frame.resetHistory;
    inputs.hdr = frame.hdr;
    inputs.cameraCut = frame.cameraCut;
    return inputs;
}

} // namespace nrfusion
