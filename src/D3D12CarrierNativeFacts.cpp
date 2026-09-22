#include "nrfusion/D3D12CarrierNativeFacts.hpp"

namespace nrfusion {
namespace {

bool ValidTexture(const D3D12NativeTextureFacts& texture) noexcept {
    return texture.opaqueId != 0 && texture.texture2D &&
           texture.resolution.Valid() &&
           texture.format != ResourceFormat::Unknown &&
           texture.depthOrArraySize == 1 &&
           texture.mipLevels == 1 &&
           texture.sampleCount == 1;
}

bool HasEvidence(const D3D12NativeResourceCapture& capture) noexcept {
    return capture.provenance != ResourceProvenance::Unknown &&
           capture.reliability != ResourceReliability::Unknown;
}

D3D12AcquiredResource ToAcquired(
    const D3D12NativeResourceCapture& capture, FrameId frameId) noexcept {
    if (capture.texture.opaqueId == 0) return {};

    ResourceRef resource{};
    resource.opaqueId = capture.texture.opaqueId;
    resource.resolution = capture.texture.resolution;
    resource.format = capture.texture.format;
    resource.provenance = capture.provenance;
    resource.reliability = capture.reliability;
    resource.ownership = ResourceOwnership::Borrowed;
    resource.lifetime = ResourceLifetime::Frame;
    resource.sourceFrameId = frameId;
    return {resource, true};
}

D3D12NativeAcquireFailure ValidateCapture(
    const D3D12NativeResourceCapture& capture, bool required) noexcept {
    if (capture.texture.opaqueId == 0)
        return required ? D3D12NativeAcquireFailure::MissingColor
                        : D3D12NativeAcquireFailure::None;
    if (!ValidTexture(capture.texture))
        return D3D12NativeAcquireFailure::InvalidTexture;
    if (!HasEvidence(capture))
        return D3D12NativeAcquireFailure::InvalidEvidence;
    return D3D12NativeAcquireFailure::None;
}

} // namespace

D3D12NativeAcquireResult BuildD3D12NativeAcquireSnapshot(
    const D3D12NativeAcquireInput& input) noexcept {
    D3D12NativeAcquireResult result{};

    if (input.identity.frameId == 0 ||
        input.identity.configurationGeneration == 0) {
        result.failure = D3D12NativeAcquireFailure::InvalidIdentity;
        return result;
    }
    if (input.output.opaqueId == 0) {
        result.failure = D3D12NativeAcquireFailure::MissingOutput;
        return result;
    }
    if (!ValidTexture(input.output)) {
        result.failure = D3D12NativeAcquireFailure::InvalidTexture;
        return result;
    }

    const D3D12NativeResourceCapture* captures[] = {
        &input.color,
        &input.depth,
        &input.motionVectors,
        &input.exposure,
        &input.reactiveMask
    };
    for (std::size_t i = 0; i < std::size(captures); ++i) {
        const auto failure = ValidateCapture(*captures[i], i == 0);
        if (failure != D3D12NativeAcquireFailure::None) {
            result.failure = failure;
            return result;
        }
    }

    auto& snapshot = result.snapshot;
    snapshot.identity = input.identity;
    snapshot.renderResolution = input.color.texture.resolution;
    snapshot.outputResolution = input.output.resolution;
    snapshot.jitter = input.jitter;
    snapshot.color = ToAcquired(input.color, input.identity.frameId);
    snapshot.depth = ToAcquired(input.depth, input.identity.frameId);
    snapshot.motionVectors = ToAcquired(input.motionVectors, input.identity.frameId);
    snapshot.exposure = ToAcquired(input.exposure, input.identity.frameId);
    snapshot.reactiveMask = ToAcquired(input.reactiveMask, input.identity.frameId);
    snapshot.hdr = input.hdr;
    snapshot.cameraCut = input.cameraCut;
    snapshot.resetHistory = input.resetHistory;
    return result;
}

} // namespace nrfusion
