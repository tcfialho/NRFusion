#include "nrfusion/D3D12CarrierNativeAcquire.hpp"
#include "nrfusion/D3D12GuideFormat.hpp"

#include <limits>

namespace nrfusion {
namespace {

ResourceFormat MapTypedFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM: return ResourceFormat::R8Unorm;
    case DXGI_FORMAT_R16_UNORM: return ResourceFormat::R16Unorm;
    case DXGI_FORMAT_R16_FLOAT: return ResourceFormat::R16Float;
    case DXGI_FORMAT_R32_FLOAT: return ResourceFormat::R32Float;
    case DXGI_FORMAT_R16G16_FLOAT: return ResourceFormat::Rg16Float;
    case DXGI_FORMAT_R32G32_FLOAT: return ResourceFormat::Rg32Float;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return ResourceFormat::Rgba8Unorm;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return ResourceFormat::Rgba16Float;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return ResourceFormat::Rgba32Float;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return ResourceFormat::R24UnormX8;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return ResourceFormat::R32FloatX8X24;
    case DXGI_FORMAT_D32_FLOAT: return ResourceFormat::D32Float;
    default: return ResourceFormat::Unknown;
    }
}

bool TypelessFamily(
    DXGI_FORMAT format, D3D12TypelessGuideFamily& family) noexcept {
    switch (format) {
    case DXGI_FORMAT_R32_TYPELESS:
        family = D3D12TypelessGuideFamily::R32; return true;
    case DXGI_FORMAT_R16_TYPELESS:
        family = D3D12TypelessGuideFamily::R16; return true;
    case DXGI_FORMAT_R24G8_TYPELESS:
        family = D3D12TypelessGuideFamily::R24G8; return true;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        family = D3D12TypelessGuideFamily::R32G8X24; return true;
    case DXGI_FORMAT_R32G32_TYPELESS:
        family = D3D12TypelessGuideFamily::R32G32; return true;
    case DXGI_FORMAT_R16G16_TYPELESS:
        family = D3D12TypelessGuideFamily::R16G16; return true;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        family = D3D12TypelessGuideFamily::R8G8B8A8; return true;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        family = D3D12TypelessGuideFamily::R16G16B16A16; return true;
    default:
        return false;
    }
}

ResourceFormat MapFormat(
    DXGI_FORMAT format, const D3D12GuideRole* guideRole) noexcept {
    const ResourceFormat typed = MapTypedFormat(format);
    if (typed != ResourceFormat::Unknown) {
        return guideRole == nullptr
            ? typed
            : NormalizeD3D12TypedGuideFormat(*guideRole, typed);
    }
    if (guideRole == nullptr) return ResourceFormat::Unknown;

    D3D12TypelessGuideFamily family{};
    if (!TypelessFamily(format, family)) return ResourceFormat::Unknown;
    return NormalizeD3D12TypelessGuideFormat(*guideRole, family);
}

D3D12NativeTextureFacts Describe(
    ID3D12Resource* resource, const D3D12GuideRole* guideRole = nullptr) noexcept {
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
    facts.format = MapFormat(desc.Format, guideRole);
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
    const D3D12NativeResourceInput& input,
    const D3D12GuideRole* guideRole = nullptr) noexcept {
    return {
        Describe(input.resource, guideRole),
        input.provenance,
        input.reliability
    };
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
    const D3D12GuideRole depthRole = D3D12GuideRole::Depth;
    const D3D12GuideRole motionRole = D3D12GuideRole::Motion;
    input.color = Capture(resources.color);
    input.depth = Capture(resources.depth, &depthRole);
    input.motionVectors = Capture(resources.motionVectors, &motionRole);
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
