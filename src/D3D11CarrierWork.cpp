#include "nrfusion/D3D11CarrierWork.hpp"

#include <cmath>

namespace nrfusion {
namespace {

bool TrustedOptionalResource(const ResourceRef& resource, FrameId frameId) noexcept {
    if (!resource.Valid()) return true;
    return resource.EvidenceWellFormed() && resource.BelongsToFrame(frameId) &&
        resource.reliability != ResourceReliability::Unreliable;
}

} // namespace

std::optional<SyntheticFrameInputs> BuildD3D11CarrierWork(
    const FrameContext& frame, const WorkTicket& ticket,
    float workingScale) noexcept {
    if (frame.api != GraphicsApi::D3D11 || !frame.ReadyForCore() ||
        ticket.id == 0 || ticket.session == 0 ||
        !std::isfinite(workingScale) || workingScale <= 0.0f ||
        workingScale > 1.0f)
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
    inputs.workingScale = workingScale;
    inputs.reset = frame.resetHistory;
    inputs.hdr = frame.hdr;
    inputs.cameraCut = frame.cameraCut;
    inputs.motionSource = frame.EffectiveMotionSource();
    return inputs;
}

} // namespace nrfusion
