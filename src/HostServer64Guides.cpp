#include "nrfusion/HostServer64.hpp"

namespace nrfusion {

bool HostServer64::EnsureZeroGuides(uint32_t width, uint32_t height) {
    if (lowGuideDepth_ && lowGuideMotion_ && guideWidth_ == width && guideHeight_ == height) {
        return true;
    }
    if (width == 0 || height == 0 || !d3d12Device_) return false;
    if (guideFence_ && guideFenceValue_ != 0 &&
        guideFence_->GetCompletedValue() < guideFenceValue_) {
        return false;
    }
    if (d3d12Fence_ && guideUseFenceValue_ != 0 &&
        d3d12Fence_->GetCompletedValue() < guideUseFenceValue_) {
        return false;
    }

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 uploadSize = 0;
    d3d12Device_->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    zeroGuideUpload_.Reset();
    if (FAILED(d3d12Device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&zeroGuideUpload_)))) {
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(zeroGuideUpload_->Map(0, nullptr, &mapped))) return false;
    ZeroMemory(mapped, static_cast<size_t>(uploadSize));
    zeroGuideUpload_->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    lowGuideDepth_.Reset();
    lowGuideMotion_.Reset();
    if (FAILED(d3d12Device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&lowGuideDepth_)))) {
        return false;
    }
    if (FAILED(d3d12Device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&lowGuideMotion_)))) {
        return false;
    }

    if (!guideAlloc_) {
        if (FAILED(d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&guideAlloc_)))) return false;
    }
    if (!guideCmdList_) {
        if (FAILED(d3d12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, guideAlloc_.Get(),
                                                   nullptr, IID_PPV_ARGS(&guideCmdList_)))) return false;
        guideCmdList_->Close();
    }
    if (!guideFence_) {
        if (FAILED(d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&guideFence_)))) return false;
    }

    guideAlloc_->Reset();
    guideCmdList_->Reset(guideAlloc_.Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = zeroGuideUpload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    for (ID3D12Resource* dst : { lowGuideDepth_.Get(), lowGuideMotion_.Get() }) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dst;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        guideCmdList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = dst;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        guideCmdList_->ResourceBarrier(1, &barrier);
    }

    guideCmdList_->Close();
    ID3D12CommandList* lists[] = { guideCmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);

    const uint64_t fenceVal = guideFenceValue_ + 1;
    if (FAILED(d3d12Queue_->Signal(guideFence_.Get(), fenceVal))) return false;
    guideFenceValue_ = fenceVal;
    guideUseFenceValue_ = 0;

    guideWidth_ = width;
    guideHeight_ = height;
    return true;
}

} // namespace nrfusion
