#include "nrfusion/D3D12CarrierNativeAcquire.hpp"

#include <limits>

namespace nrfusion {
namespace {

ResourceFormat MapFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM: return ResourceFormat::R8Unorm;
    case DXGI_FORMAT_R16_FLOAT: return ResourceFormat::R16Float;
    case DXGI_FORMAT_R32_FLOAT: return ResourceFormat::R32Float;
    case DXGI_FORMAT_R16G16_FLOAT: return ResourceFormat::Rg16Float;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return ResourceFormat::Rgba16Float;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return ResourceFormat::Rgba32Float;
    case DXGI_FORMAT_D32_FLOAT: return ResourceFormat::D32Float;
    default: return ResourceFormat::Unknown;
    }
}

D3D12NativeTextureFacts Describe(ID3D12Resource* resource) noexcept {
    D3D12NativeTextureFacts facts{};
    if (resource == nullptr) return facts;

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    facts.opaqueId = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(resource));
    facts.texture2D = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    if (desc.Width <=
        static_cast<UINT64>((std::numeric_limits<std::uint32_t>::max)())) {
        facts.resolution.width = static_cast<std::uint32_t>(desc.Width);
    }
    facts.resolution.height = desc.Height;
    facts.format = MapFormat(desc.Format);
    facts.depthOrArraySize = desc.DepthOrArraySize;
    facts.mipLevels = desc.MipLevels;
    facts.sampleCount = desc.SampleDesc.Count;
    return facts;
}

bool EvidenceReady(const D3D12NativeResourceInput& input) noexcept {
    return input.provenance != ResourceProvenance::Unknown &&
           input.reliability != ResourceReliability::Unknown;
}

D3D12NativeResourceCapture Capture(
    const D3D12NativeResourceInput& input) noexcept {
    return {Describe(input.resource), input.provenance, input.reliability};
}

} // namespace

D3D12NativeAcquireResult AcquireD3D12NativeSnapshot(
    const D3D12NativeFrameResources& resources) noexcept {
    if (resources.identity.frameId == 0 ||
        resources.identity.configurationGeneration == 0)
        return {{}, D3D12NativeAcquireFailure::InvalidIdentity};
    if (resources.color.resource == nullptr)
        return {{}, D3D12NativeAcquireFailure::MissingColor};
    if (resources.output == nullptr)
        return {{}, D3D12NativeAcquireFailure::MissingOutput};

    const D3D12NativeResourceInput* captured[] = {
        &resources.color,
        &resources.depth,
        &resources.motionVectors,
        &resources.exposure,
        &resources.reactiveMask
    };
    for (const auto* input : captured) {
        if (input->resource != nullptr && !EvidenceReady(*input))
            return {{}, D3D12NativeAcquireFailure::InvalidEvidence};
    }

    D3D12NativeAcquireInput input{};
    input.identity = resources.identity;
    input.color = Capture(resources.color);
    input.depth = Capture(resources.depth);
    input.motionVectors = Capture(resources.motionVectors);
    input.exposure = Capture(resources.exposure);
    input.reactiveMask = Capture(resources.reactiveMask);
    input.output = Describe(resources.output);
    input.jitter = resources.jitter;
    input.hdr = resources.hdr;
    input.cameraCut = resources.cameraCut;
    input.resetHistory = resources.resetHistory;
    return BuildD3D12NativeAcquireSnapshot(input);
}

} // namespace nrfusion
