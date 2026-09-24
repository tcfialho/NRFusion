#include "nrfusion/SyntheticDx11BridgeProvider.hpp"

namespace nrfusion {

bool SyntheticDx11BridgeProvider::CreatePrivateD3D12() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3d11Device_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
    if (FAILED(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device_))))
        return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d3d12Device_->CreateCommandQueue(
            &queueDesc, IID_PPV_ARGS(&d3d12Queue_))))
        return false;

    for (auto& slot : sharedSlots_) {
        if (FAILED(d3d12Device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.alloc))))
            return false;
    }
    if (FAILED(d3d12Device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, sharedSlots_[0].alloc.Get(),
            nullptr, IID_PPV_ARGS(&d3d12CmdList_))))
        return false;
    if (FAILED(d3d12CmdList_->Close())) return false;
    return SUCCEEDED(d3d12Device_->CreateFence(
        0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence_)));
}

void SyntheticDx11BridgeProvider::CloseSharedHandles() {
    for (auto& slot : sharedSlots_) {
        if (slot.colorSharedHandle) CloseHandle(slot.colorSharedHandle);
        if (slot.residualSharedHandle) CloseHandle(slot.residualSharedHandle);
        slot.colorSharedHandle = nullptr;
        slot.residualSharedHandle = nullptr;
        slot.d3d11Color.Reset();
        slot.d3d11Residual.Reset();
        slot.d3d12Color.Reset();
        slot.d3d12Residual.Reset();
    }
    slotTracker_.Reset();
    currentRes_ = {};
}

bool SyntheticDx11BridgeProvider::CreateSharedResources(
    std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (currentRes_.width == width && currentRes_.height == height) return true;
    if (currentRes_.Valid() &&
        !slotTracker_.AllRetired(d3d12Fence_->GetCompletedValue()))
        return false;
    CloseSharedHandles();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    for (auto& slot : sharedSlots_) {
        if (FAILED(d3d11Device_->CreateTexture2D(
                &desc, nullptr, &slot.d3d11Color)))
            return false;
        ComPtr<IDXGIResource1> color;
        if (FAILED(slot.d3d11Color.As(&color)) ||
            FAILED(color->CreateSharedHandle(
                nullptr, GENERIC_ALL, nullptr, &slot.colorSharedHandle)) ||
            FAILED(d3d12Device_->OpenSharedHandle(
                slot.colorSharedHandle, IID_PPV_ARGS(&slot.d3d12Color))))
            return false;

        if (FAILED(d3d11Device_->CreateTexture2D(
                &desc, nullptr, &slot.d3d11Residual)))
            return false;
        ComPtr<IDXGIResource1> residual;
        if (FAILED(slot.d3d11Residual.As(&residual)) ||
            FAILED(residual->CreateSharedHandle(
                nullptr, GENERIC_ALL, nullptr, &slot.residualSharedHandle)) ||
            FAILED(d3d12Device_->OpenSharedHandle(
                slot.residualSharedHandle, IID_PPV_ARGS(&slot.d3d12Residual))))
            return false;
    }

    nvof_.Shutdown();
    if (!nvof_.Initialize(
            d3d12Device_.Get(), d3d12Queue_.Get(), width, height)) {
        CloseSharedHandles();
        return false;
    }
    currentRes_ = {width, height};
    return true;
}

bool SyntheticDx11BridgeProvider::CopyInputToSlot(
    std::uint32_t slot, ID3D11Resource* gameColor) {
    if (slot >= kMaxInFlight || gameColor == nullptr || !d3d11Context_)
        return false;
    SharedSlot& target = sharedSlots_[slot];
    if (!D3D11ResourcesCopyCompatible(
            gameColor, target.d3d11Color.Get(), d3d11Device_.Get()))
        return false;
    d3d11Context_->CopyResource(target.d3d11Color.Get(), gameColor);
    return true;
}

} // namespace nrfusion
