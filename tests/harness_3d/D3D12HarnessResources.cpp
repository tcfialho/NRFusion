#include "D3D12TestHarness.hpp"

namespace nrfusion::testing {

bool D3D12TestHarness::CreateRenderTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = 3;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)))) return false;

    const auto rtvHandleSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = config_.width;
    resDesc.Height = config_.height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // 1. Color: HDR R16G16B16A16_FLOAT
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
                                               IID_PPV_ARGS(&colorBuffer_))))
        return false;
    device_->CreateRenderTargetView(colorBuffer_.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += rtvHandleSize;

    // 2. Motion Vectors: R16G16_FLOAT
    resDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    D3D12_CLEAR_VALUE motionClear{};
    motionClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &motionClear,
                                               IID_PPV_ARGS(&motionBuffer_))))
        return false;
    device_->CreateRenderTargetView(motionBuffer_.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += rtvHandleSize;

    // 3. Reactive Mask: R8_UNORM
    resDesc.Format = DXGI_FORMAT_R8_UNORM;
    D3D12_CLEAR_VALUE reactiveClear{};
    reactiveClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &reactiveClear,
                                               IID_PPV_ARGS(&reactiveBuffer_))))
        return false;
    device_->CreateRenderTargetView(reactiveBuffer_.Get(), nullptr, rtvHandle);

    // 4. Depth Stencil: D32_FLOAT
    resDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = resDesc.Format;
    depthClear.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                               IID_PPV_ARGS(&depthBuffer_))))
        return false;
    device_->CreateDepthStencilView(depthBuffer_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    // 5. Exposure Buffer: 1x1 R32_FLOAT
    resDesc.Width = 1;
    resDesc.Height = 1;
    resDesc.Format = DXGI_FORMAT_R32_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&exposureBuffer_))))
        return false;

    // 6. Compute Scratch Buffer: 1024 floats (RWStructuredBuffer)
    D3D12_RESOURCE_DESC scratchDesc{};
    scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    scratchDesc.Width = 1024 * sizeof(float);
    scratchDesc.Height = 1;
    scratchDesc.DepthOrArraySize = 1;
    scratchDesc.MipLevels = 1;
    scratchDesc.SampleDesc.Count = 1;
    scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    scratchDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &scratchDesc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&computeScratchBuffer_))))
        return false;

    return true;
}


bool D3D12TestHarness::ResizeResources(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return false;

    if (directQueue_ && directFence_) {
        const auto value = ++directFenceValue_;
        if (FAILED(directQueue_->Signal(directFence_.Get(), value))) return false;
        if (directFence_->GetCompletedValue() < value) {
            if (FAILED(directFence_->SetEventOnCompletion(value, fenceEvent_))) return false;
            if (WaitForSingleObject(fenceEvent_, 2000) != WAIT_OBJECT_0) return false;
        }
    }
    if (computeQueue_ && computeFence_) {
        const auto value = ++computeFenceValue_;
        if (FAILED(computeQueue_->Signal(computeFence_.Get(), value))) return false;
        if (computeFence_->GetCompletedValue() < value) {
            if (FAILED(computeFence_->SetEventOnCompletion(value, fenceEvent_))) return false;
            if (WaitForSingleObject(fenceEvent_, 2000) != WAIT_OBJECT_0) return false;
        }
    }

    colorBuffer_.Reset();
    depthBuffer_.Reset();
    motionBuffer_.Reset();
    exposureBuffer_.Reset();
    reactiveBuffer_.Reset();
    computeScratchBuffer_.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();

    config_.width = width;
    config_.height = height;
    hasPrevFrame_ = false;
    return CreateRenderTargets();
}

} // namespace nrfusion::testing
