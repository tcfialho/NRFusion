#include "nrfusion/D3D12CarrierContract.hpp"

#include <cmath>
#include <iterator>

namespace nrfusion {
namespace {

bool Mentioned(const ResourceRef& resource) noexcept {
    return resource.opaqueId != 0 || resource.resolution.Valid() ||
           resource.format != ResourceFormat::Unknown ||
           resource.provenance != ResourceProvenance::Unknown ||
           resource.reliability != ResourceReliability::Unknown ||
           resource.ownership != ResourceOwnership::Unknown ||
           resource.lifetime != ResourceLifetime::Unknown ||
           resource.sourceFrameId != 0;
}

D3D12AcquireFailure ValidateResource(
    const D3D12AcquiredResource& acquired, FrameId frameId,
    bool required) noexcept {
    if (!acquired.acquired) {
        if (Mentioned(acquired.resource)) return D3D12AcquireFailure::UnprovenResource;
        return required ? D3D12AcquireFailure::MissingColor
                        : D3D12AcquireFailure::None;
    }
    if (!acquired.resource.Valid()) return D3D12AcquireFailure::InvalidResource;
    if (!acquired.resource.EvidenceExplicit() ||
        !acquired.resource.EvidenceWellFormed())
        return D3D12AcquireFailure::InvalidEvidence;
    if (!acquired.resource.BelongsToFrame(frameId))
        return D3D12AcquireFailure::StaleResource;
    return D3D12AcquireFailure::None;
}

ResourceRef PresentResource(const D3D12AcquiredResource& acquired) noexcept {
    return acquired.acquired ? acquired.resource : ResourceRef{};
}

} // namespace

D3D12AcquireResult BuildD3D12FrameContract(
    const D3D12AcquireSnapshot& snapshot) noexcept {
    D3D12AcquireResult result{};
    result.attempted = true;

    if (snapshot.identity.frameId == 0 ||
        snapshot.identity.configurationGeneration == 0) {
        result.failure = D3D12AcquireFailure::InvalidIdentity;
        return result;
    }
    if (!snapshot.renderResolution.Valid() || !snapshot.outputResolution.Valid()) {
        result.failure = D3D12AcquireFailure::InvalidDimensions;
        return result;
    }
    if (!std::isfinite(snapshot.jitter.x) || !std::isfinite(snapshot.jitter.y)) {
        result.failure = D3D12AcquireFailure::InvalidJitter;
        return result;
    }

    const D3D12AcquiredResource* resources[] = {
        &snapshot.color,
        &snapshot.depth,
        &snapshot.motionVectors,
        &snapshot.exposure,
        &snapshot.reactiveMask
    };
    for (std::size_t i = 0; i < std::size(resources); ++i) {
        const auto failure = ValidateResource(
            *resources[i], snapshot.identity.frameId, i == 0);
        if (failure != D3D12AcquireFailure::None) {
            result.failure = failure;
            return result;
        }
    }
    if (snapshot.color.resource.resolution != snapshot.renderResolution) {
        result.failure = D3D12AcquireFailure::ColorResolutionMismatch;
        return result;
    }

    result.outputOpaqueId = snapshot.outputOpaqueId;
    FrameContext& frame = result.frame;
    frame.frameId = snapshot.identity.frameId;
    frame.hostFrameToken = snapshot.identity.hostFrameToken;
    frame.viewId = snapshot.identity.viewId;
    frame.configurationGeneration = snapshot.identity.configurationGeneration;
    frame.renderResolution = snapshot.renderResolution;
    frame.outputResolution = snapshot.outputResolution;
    frame.jitter = snapshot.jitter;
    frame.color = PresentResource(snapshot.color);
    frame.depth = PresentResource(snapshot.depth);
    frame.motionVectors = PresentResource(snapshot.motionVectors);
    frame.exposure = PresentResource(snapshot.exposure);
    frame.reactiveMask = PresentResource(snapshot.reactiveMask);
    frame.hdr = snapshot.hdr;
    frame.cameraCut = snapshot.cameraCut;
    frame.resetHistory = snapshot.resetHistory;
    frame.api = GraphicsApi::D3D12;
    frame.depthReliable =
        frame.depth.reliability == ResourceReliability::Reliable;
    frame.motionVectorSource = frame.EffectiveMotionSource();
    frame.motionVectorsReliable =
        frame.motionVectors.reliability == ResourceReliability::Reliable;

    if (!frame.ReadyForCore()) {
        result.failure = D3D12AcquireFailure::InvalidEvidence;
        return result;
    }

    result.diagnostics.supported = true;
    result.diagnostics.frameComplete = true;
    result.diagnostics.depthValid = frame.DepthReliable();
    result.diagnostics.motionValid =
        frame.MotionReliable(frame.EffectiveMotionSource());
    result.diagnostics.exposureValid = frame.ExposureReliable();
    return result;
}

} // namespace nrfusion
