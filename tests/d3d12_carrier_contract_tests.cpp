#include "nrfusion/D3D12CarrierContract.hpp"

#include <cassert>
#include <limits>
#include <type_traits>

using namespace nrfusion;

namespace {

ResourceRef Acquired(
    std::uint64_t id, FrameId frameId, Resolution resolution,
    ResourceFormat format, ResourceProvenance provenance,
    ResourceReliability reliability = ResourceReliability::Reliable) {
    ResourceRef resource{};
    resource.opaqueId = id;
    resource.resolution = resolution;
    resource.format = format;
    resource.provenance = provenance;
    resource.reliability = reliability;
    resource.ownership = ResourceOwnership::Borrowed;
    resource.lifetime = ResourceLifetime::Frame;
    resource.sourceFrameId = frameId;
    return resource;
}

D3D12AcquireSnapshot BaseSnapshot() {
    D3D12AcquireSnapshot snapshot{};
    snapshot.identity.frameId = 42;
    snapshot.identity.hostFrameToken = 9001;
    snapshot.identity.viewId = 7;
    snapshot.identity.configurationGeneration = 3;
    snapshot.renderResolution = {1920, 1080};
    snapshot.outputResolution = {3840, 2160};
    snapshot.outputOpaqueId = 9;
    snapshot.color = {
        Acquired(1, 42, snapshot.renderResolution, ResourceFormat::Rgba16Float,
                 ResourceProvenance::GameNative),
        true
    };
    return snapshot;
}

} // namespace

int main() {
    static_assert(std::is_trivially_copyable_v<D3D12AcquiredResource>);
    static_assert(std::is_standard_layout_v<D3D12AcquiredResource>);
    static_assert(std::is_trivially_copyable_v<D3D12AcquireSnapshot>);
    static_assert(std::is_standard_layout_v<D3D12AcquireSnapshot>);

    const auto base = BaseSnapshot();
    const auto acquired = BuildD3D12FrameContract(base);
    assert(acquired.attempted);
    assert(acquired);
    assert(acquired.frame.api == GraphicsApi::D3D12);
    assert(acquired.frame.frameId == base.identity.frameId);
    assert(acquired.frame.hostFrameToken == base.identity.hostFrameToken);
    assert(acquired.frame.viewId == base.identity.viewId);
    assert(acquired.frame.configurationGeneration ==
           base.identity.configurationGeneration);
    assert(acquired.frame.color.opaqueId == base.color.resource.opaqueId);
    assert(acquired.frame.color.resolution == base.color.resource.resolution);
    assert(acquired.frame.color.format == base.color.resource.format);
    assert(acquired.frame.ReadyForCore());
    assert(acquired.diagnostics.supported);
    assert(acquired.diagnostics.frameComplete);
    assert(!acquired.diagnostics.depthValid);
    assert(!acquired.diagnostics.motionValid);
    assert(!acquired.diagnostics.exposureValid);

    auto missingOutput = base;
    missingOutput.outputOpaqueId = 0;
    assert(BuildD3D12FrameContract(missingOutput).failure ==
           D3D12AcquireFailure::MissingOutput);

    auto missingColor = base;
    missingColor.color = {};
    assert(BuildD3D12FrameContract(missingColor).failure ==
           D3D12AcquireFailure::MissingColor);

    auto unprovenColor = base;
    unprovenColor.color.acquired = false;
    assert(BuildD3D12FrameContract(unprovenColor).failure ==
           D3D12AcquireFailure::UnprovenResource);

    auto staleColor = base;
    staleColor.color.resource.sourceFrameId = base.identity.frameId - 1;
    assert(BuildD3D12FrameContract(staleColor).failure ==
           D3D12AcquireFailure::StaleResource);

    auto malformedEvidence = base;
    malformedEvidence.color.resource.ownership = ResourceOwnership::Unknown;
    assert(BuildD3D12FrameContract(malformedEvidence).failure ==
           D3D12AcquireFailure::InvalidEvidence);

    auto wrongColorSize = base;
    wrongColorSize.color.resource.resolution = {1280, 720};
    assert(BuildD3D12FrameContract(wrongColorSize).failure ==
           D3D12AcquireFailure::ColorResolutionMismatch);

    auto unprovenOptional = base;
    unprovenOptional.depth.resource =
        Acquired(2, 42, base.renderResolution, ResourceFormat::D32Float,
                 ResourceProvenance::GameNative);
    assert(BuildD3D12FrameContract(unprovenOptional).failure ==
           D3D12AcquireFailure::UnprovenResource);

    auto full = base;
    full.depth = {
        Acquired(2, 42, base.renderResolution, ResourceFormat::D32Float,
                 ResourceProvenance::GameNative),
        true
    };
    full.motionVectors = {
        Acquired(3, 42, base.renderResolution, ResourceFormat::Rg16Float,
                 ResourceProvenance::DlssContract),
        true
    };
    full.exposure = {
        Acquired(4, 42, {1, 1}, ResourceFormat::R32Float,
                 ResourceProvenance::DlssContract),
        true
    };
    const auto complete = BuildD3D12FrameContract(full);
    assert(complete);
    assert(complete.frame.DepthReliable());
    assert(complete.frame.EffectiveMotionSource() == MotionSource::DlssContract);
    assert(complete.frame.MotionReliable(MotionSource::DlssContract));
    assert(complete.frame.ExposureReliable());
    assert(complete.diagnostics.depthValid);
    assert(complete.diagnostics.motionValid);
    assert(complete.diagnostics.exposureValid);

    auto badIdentity = base;
    badIdentity.identity.configurationGeneration = 0;
    assert(BuildD3D12FrameContract(badIdentity).failure ==
           D3D12AcquireFailure::InvalidIdentity);

    auto badJitter = base;
    badJitter.jitter.x = std::numeric_limits<float>::quiet_NaN();
    assert(BuildD3D12FrameContract(badJitter).failure ==
           D3D12AcquireFailure::InvalidJitter);

    return 0;
}
