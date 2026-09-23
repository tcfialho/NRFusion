#include "nrfusion/D3D12CarrierNativeFacts.hpp"

#include <cassert>
#include <type_traits>

using namespace nrfusion;

namespace {

D3D12NativeTextureFacts Texture(
    std::uint64_t id, Resolution resolution, ResourceFormat format) {
    D3D12NativeTextureFacts texture{};
    texture.opaqueId = id;
    texture.resolution = resolution;
    texture.format = format;
    texture.depthOrArraySize = 1;
    texture.mipLevels = 1;
    texture.sampleCount = 1;
    texture.texture2D = true;
    return texture;
}

D3D12NativeResourceCapture Capture(
    std::uint64_t id, Resolution resolution, ResourceFormat format,
    ResourceProvenance provenance) {
    return {
        Texture(id, resolution, format),
        provenance,
        ResourceReliability::Reliable
    };
}

D3D12NativeAcquireInput BaseInput() {
    D3D12NativeAcquireInput input{};
    input.identity.frameId = 21;
    input.identity.hostFrameToken = 41;
    input.identity.viewId = 2;
    input.identity.configurationGeneration = 5;
    input.color = Capture(
        100, {1920, 1080}, ResourceFormat::Rgba16Float,
        ResourceProvenance::GameNative);
    input.output = Texture(
        200, {3840, 2160}, ResourceFormat::Rgba16Float);
    input.jitter = {0.25f, -0.25f};
    input.hdr = true;
    return input;
}

} // namespace

int main() {
    static_assert(std::is_trivially_copyable_v<D3D12NativeTextureFacts>);
    static_assert(std::is_standard_layout_v<D3D12NativeAcquireInput>);

    const auto base = BaseInput();
    const auto built = BuildD3D12NativeAcquireSnapshot(base);
    assert(built);
    assert(built.snapshot.renderResolution == base.color.texture.resolution);
    assert(built.snapshot.outputResolution == base.output.resolution);
    assert(built.snapshot.outputOpaqueId == base.output.opaqueId);
    assert(built.snapshot.color.acquired);
    assert(built.snapshot.color.resource.opaqueId == base.color.texture.opaqueId);
    assert(built.snapshot.color.resource.ownership == ResourceOwnership::Borrowed);
    assert(built.snapshot.color.resource.lifetime == ResourceLifetime::Frame);
    assert(built.snapshot.color.resource.sourceFrameId == base.identity.frameId);
    assert(built.snapshot.hdr);

    const auto contract = BuildD3D12FrameContract(built.snapshot);
    assert(contract);
    assert(contract.frame.renderResolution == base.color.texture.resolution);
    assert(contract.frame.outputResolution == base.output.resolution);
    assert(contract.outputOpaqueId == base.output.opaqueId);

    auto missingColor = base;
    missingColor.color = {};
    assert(BuildD3D12NativeAcquireSnapshot(missingColor).failure ==
           D3D12NativeAcquireFailure::MissingColor);

    auto missingOutput = base;
    missingOutput.output = {};
    assert(BuildD3D12NativeAcquireSnapshot(missingOutput).failure ==
           D3D12NativeAcquireFailure::MissingOutput);

    auto arrayColor = base;
    arrayColor.color.texture.depthOrArraySize = 2;
    assert(BuildD3D12NativeAcquireSnapshot(arrayColor).failure ==
           D3D12NativeAcquireFailure::InvalidTexture);

    auto mipColor = base;
    mipColor.color.texture.mipLevels = 2;
    assert(BuildD3D12NativeAcquireSnapshot(mipColor).failure ==
           D3D12NativeAcquireFailure::InvalidTexture);

    auto msaaOutput = base;
    msaaOutput.output.sampleCount = 4;
    assert(BuildD3D12NativeAcquireSnapshot(msaaOutput).failure ==
           D3D12NativeAcquireFailure::InvalidTexture);

    auto unknownFormat = base;
    unknownFormat.color.texture.format = ResourceFormat::Unknown;
    assert(BuildD3D12NativeAcquireSnapshot(unknownFormat).failure ==
           D3D12NativeAcquireFailure::InvalidTexture);

    auto unknownOutputFormat = base;
    unknownOutputFormat.output.format = ResourceFormat::Unknown;
    assert(BuildD3D12NativeAcquireSnapshot(unknownOutputFormat));

    auto missingEvidence = base;
    missingEvidence.color.provenance = ResourceProvenance::Unknown;
    assert(BuildD3D12NativeAcquireSnapshot(missingEvidence).failure ==
           D3D12NativeAcquireFailure::InvalidEvidence);

    auto depth = base;
    depth.depth = Capture(
        300, {1920, 1080}, ResourceFormat::D32Float,
        ResourceProvenance::GameNative);
    const auto withDepth = BuildD3D12NativeAcquireSnapshot(depth);
    assert(withDepth);
    assert(withDepth.snapshot.depth.acquired);
    assert(withDepth.snapshot.depth.resource.sourceFrameId == base.identity.frameId);

    auto typelessDepthSemantic = base;
    typelessDepthSemantic.depth = Capture(
        301, {1920, 1080}, ResourceFormat::R24UnormX8,
        ResourceProvenance::GameNative);
    const auto normalizedDepth =
        BuildD3D12NativeAcquireSnapshot(typelessDepthSemantic);
    assert(normalizedDepth);
    assert(BuildD3D12FrameContract(normalizedDepth.snapshot));

    auto typelessMotionSemantic = base;
    typelessMotionSemantic.motionVectors = Capture(
        302, {1920, 1080}, ResourceFormat::Rg32Float,
        ResourceProvenance::DlssContract);
    const auto normalizedMotion =
        BuildD3D12NativeAcquireSnapshot(typelessMotionSemantic);
    assert(normalizedMotion);
    const auto motionContract =
        BuildD3D12FrameContract(normalizedMotion.snapshot);
    assert(motionContract);
    assert(motionContract.frame.EffectiveMotionSource() ==
           MotionSource::DlssContract);

    auto badIdentity = base;
    badIdentity.identity.frameId = 0;
    assert(BuildD3D12NativeAcquireSnapshot(badIdentity).failure ==
           D3D12NativeAcquireFailure::InvalidIdentity);
    return 0;
}
