#include "nrfusion/SyntheticDx12Provider.hpp"

#include <limits>

namespace nrfusion {
namespace {

void AddResource(
    ID3D12Resource* resource,
    SyntheticDx12ResourceAccounting& result) noexcept {
    if (resource == nullptr) return;
    ++result.resourceCount;

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 ||
        desc.SampleDesc.Count != 1 ||
        desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        result.logicalBytesExact = false;
        return;
    }

    constexpr std::uint64_t bytesPerPixel = 8;
    const std::uint64_t pixels =
        desc.Width * static_cast<std::uint64_t>(desc.Height);
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    if (pixels > max / bytesPerPixel) {
        result.logicalBytes = max;
        result.logicalBytesExact = false;
        return;
    }

    const std::uint64_t bytes = pixels * bytesPerPixel;
    if (result.logicalBytes > max - bytes) {
        result.logicalBytes = max;
        result.logicalBytesExact = false;
        return;
    }
    result.logicalBytes += bytes;
}

} // namespace

SyntheticDx12ResourceAccounting
SyntheticDx12Provider::Accounting() const noexcept {
    std::lock_guard guard(mutex_);
    SyntheticDx12ResourceAccounting result{};
    result.descriptorHeapCount = srvUavHeap_ ? 1u : 0u;
    for (const Slot& slot : ringSlots_) {
        AddResource(slot.lowColor.Get(), result);
        AddResource(slot.lowDepth.Get(), result);
        AddResource(slot.lowMotion.Get(), result);
        AddResource(slot.lowNeuralOut.Get(), result);
        AddResource(slot.lowResidual.Get(), result);
    }
    return result;
}

} // namespace nrfusion
