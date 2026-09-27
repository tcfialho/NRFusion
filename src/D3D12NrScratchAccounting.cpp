#include "nrfusion/D3D12NrScratchResources.hpp"

#include <limits>

namespace nrfusion {
namespace {

std::uint32_t BytesPerPixel(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 12;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return 8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return 4;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
        return 2;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        return 1;
    default:
        return 0;
    }
}

} // namespace

D3D12NrScratchAccounting D3D12NrScratchResources::Accounting() const noexcept {
    D3D12NrScratchAccounting result{};
    constexpr D3D12NrScratchKind kinds[] = {
        D3D12NrScratchKind::Output,
        D3D12NrScratchKind::ColorCopy,
        D3D12NrScratchKind::HdrCopy,
        D3D12NrScratchKind::PassScratch,
        D3D12NrScratchKind::ColorSmall,
        D3D12NrScratchKind::OutputNative,
        D3D12NrScratchKind::ActiveColor,
        D3D12NrScratchKind::ResidualEdited,
        D3D12NrScratchKind::ResidualHistory0,
        D3D12NrScratchKind::ResidualHistory1,
        D3D12NrScratchKind::ResidualComposed,
    };

    for (const auto kind : kinds) {
        const Surface* surface = Slot(kind);
        if (surface == nullptr || surface->resource == nullptr) continue;
        ++result.resourceCount;

        const std::uint32_t bytesPerPixel = BytesPerPixel(surface->format);
        if (bytesPerPixel == 0) {
            result.logicalBytesExact = false;
            continue;
        }

        const std::uint64_t pixels =
            static_cast<std::uint64_t>(surface->width) * surface->height;
        const auto max = std::numeric_limits<std::uint64_t>::max();
        if (pixels > max / bytesPerPixel) {
            result.logicalBytes = max;
            result.logicalBytesExact = false;
            continue;
        }

        const std::uint64_t bytes = pixels * bytesPerPixel;
        if (result.logicalBytes > max - bytes) {
            result.logicalBytes = max;
            result.logicalBytesExact = false;
            continue;
        }
        result.logicalBytes += bytes;
    }
    return result;
}

} // namespace nrfusion
