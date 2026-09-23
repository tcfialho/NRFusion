#include "nrfusion/D3D12GuideFormat.hpp"

namespace nrfusion {

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

} // namespace nrfusion
