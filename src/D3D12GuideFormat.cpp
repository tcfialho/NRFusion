#include "nrfusion/D3D12GuideFormat.hpp"

namespace nrfusion {

bool CanUseDirectD3D12Guide(bool runtimeQualified, D3D12GuideRole role,
                           D3D12TypelessGuideFamily family, Resolution surface) noexcept {
    if (!runtimeQualified) return false;
    const bool formatQualified =
        (role == D3D12GuideRole::Depth &&
            (family == D3D12TypelessGuideFamily::R32 ||
             family == D3D12TypelessGuideFamily::R16 ||
             family == D3D12TypelessGuideFamily::R24G8 ||
             family == D3D12TypelessGuideFamily::R32G8X24)) ||
        (role == D3D12GuideRole::Motion &&
            (family == D3D12TypelessGuideFamily::R32G32 ||
             family == D3D12TypelessGuideFamily::R16G16));
    const bool dimensionQualified =
        (surface.width > 0 && surface.height > 0 &&
         surface.width <= 16384 && surface.height <= 16384);
    return formatQualified && dimensionQualified;
}


ResourceFormat NormalizeD3D12TypedGuideFormat(
    D3D12GuideRole role, ResourceFormat format) noexcept {
    if (role == D3D12GuideRole::Depth) {
        switch (format) {
        case ResourceFormat::D32Float:
        case ResourceFormat::R32Float:
        case ResourceFormat::R16Unorm:
        case ResourceFormat::R24UnormX8:
        case ResourceFormat::R32FloatX8X24:
            return format;
        default:
            return ResourceFormat::Unknown;
        }
    }

    switch (format) {
    case ResourceFormat::Rg16Float:
    case ResourceFormat::Rg32Float:
    case ResourceFormat::Rgba8Unorm:
    case ResourceFormat::Rgba16Float:
    case ResourceFormat::Rgba32Float:
        return format;
    default:
        return ResourceFormat::Unknown;
    }
}

ResourceFormat NormalizeD3D12TypelessGuideFormat(
    D3D12GuideRole role,
    D3D12TypelessGuideFamily family) noexcept {
    if (role == D3D12GuideRole::Depth) {
        switch (family) {
        case D3D12TypelessGuideFamily::R32:
            return ResourceFormat::D32Float;
        case D3D12TypelessGuideFamily::R16:
            return ResourceFormat::R16Unorm;
        case D3D12TypelessGuideFamily::R24G8:
            return ResourceFormat::R24UnormX8;
        case D3D12TypelessGuideFamily::R32G8X24:
            return ResourceFormat::R32FloatX8X24;
        default:
            return ResourceFormat::Unknown;
        }
    }

    switch (family) {
    case D3D12TypelessGuideFamily::R32G32:
        return ResourceFormat::Rg32Float;
    case D3D12TypelessGuideFamily::R16G16:
        return ResourceFormat::Rg16Float;
    case D3D12TypelessGuideFamily::R8G8B8A8:
        return ResourceFormat::Rgba8Unorm;
    case D3D12TypelessGuideFamily::R16G16B16A16:
        return ResourceFormat::Rgba16Float;
    default:
        return ResourceFormat::Unknown;
    }
}

D3D12TypelessGuideFamily ClassifyD3D12GuideFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return D3D12TypelessGuideFamily::R32;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
        return D3D12TypelessGuideFamily::R16;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return D3D12TypelessGuideFamily::R24G8;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return D3D12TypelessGuideFamily::R32G8X24;
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
        return D3D12TypelessGuideFamily::R32G32;
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
        return D3D12TypelessGuideFamily::R16G16;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return D3D12TypelessGuideFamily::R8G8B8A8;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return D3D12TypelessGuideFamily::R16G16B16A16;
    default:
        return D3D12TypelessGuideFamily::Unknown;
    }
}

bool IsDirectD3D12GuideCandidate(D3D12GuideRole role, DXGI_FORMAT format) noexcept {
    const D3D12TypelessGuideFamily family = ClassifyD3D12GuideFormat(format);
    if (role == D3D12GuideRole::Depth) {
        return family == D3D12TypelessGuideFamily::R32 ||
               family == D3D12TypelessGuideFamily::R16 ||
               family == D3D12TypelessGuideFamily::R24G8 ||
               family == D3D12TypelessGuideFamily::R32G8X24;
    }
    if (role == D3D12GuideRole::Motion) {
        return family == D3D12TypelessGuideFamily::R32G32 ||
               family == D3D12TypelessGuideFamily::R16G16;
    }
    return false;
}

} // namespace nrfusion
