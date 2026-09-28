#include "nrfusion/SyntheticDx11BridgeProvider.hpp"

namespace nrfusion {
namespace {

struct PendingSharedSlot {
    ComPtr<ID3D11Texture2D> d3d11Color;
    ComPtr<ID3D11Texture2D> d3d11Residual;
    ComPtr<ID3D12Resource> d3d12Color;
    ComPtr<ID3D12Resource> d3d12Residual;
    HANDLE colorHandle = nullptr;
    HANDLE residualHandle = nullptr;

    ~PendingSharedSlot() {
        if (colorHandle) CloseHandle(colorHandle);
        if (residualHandle) CloseHandle(residualHandle);
    }

    HANDLE ReleaseColorHandle() noexcept {
        HANDLE handle = colorHandle;
        colorHandle = nullptr;
        return handle;
    }

    HANDLE ReleaseResidualHandle() noexcept {
        HANDLE handle = residualHandle;
        residualHandle = nullptr;
        return handle;
    }
};

} // namespace

bool SyntheticDx11BridgeProvider::CreatePrivateD3D12() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3d11Device_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device))))
        return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(
            &queueDesc, IID_PPV_ARGS(&queue))))
        return false;

    std::array<ComPtr<ID3D12CommandAllocator>, kMaxInFlight> allocs{};
    for (auto& alloc : allocs) {
        if (FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))))
            return false;
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocs[0].Get(),
            nullptr, IID_PPV_ARGS(&commandList))) ||
        FAILED(commandList->Close()))
        return false;

    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))))
        return false;

    d3d12Device_ = std::move(device);
    d3d12Queue_ = std::move(queue);
    d3d12CmdList_ = std::move(commandList);
    d3d12Fence_ = std::move(fence);
    for (std::uint32_t i = 0; i < kMaxInFlight; ++i)
        sharedSlots_[i].alloc = std::move(allocs[i]);
    return true;
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

    std::array<PendingSharedSlot, kMaxInFlight> pending{};
    for (auto& slot : pending) {
        if (FAILED(d3d11Device_->CreateTexture2D(
                &desc, nullptr, &slot.d3d11Color)))
            return false;
        ComPtr<IDXGIResource1> color;
        if (FAILED(slot.d3d11Color.As(&color)) ||
            FAILED(color->CreateSharedHandle(
                nullptr, GENERIC_ALL, nullptr, &slot.colorHandle)) ||
            FAILED(d3d12Device_->OpenSharedHandle(
                slot.colorHandle, IID_PPV_ARGS(&slot.d3d12Color))))
            return false;

        if (FAILED(d3d11Device_->CreateTexture2D(
                &desc, nullptr, &slot.d3d11Residual)))
            return false;
        ComPtr<IDXGIResource1> residual;
        if (FAILED(slot.d3d11Residual.As(&residual)) ||
            FAILED(residual->CreateSharedHandle(
                nullptr, GENERIC_ALL, nullptr, &slot.residualHandle)) ||
            FAILED(d3d12Device_->OpenSharedHandle(
                slot.residualHandle, IID_PPV_ARGS(&slot.d3d12Residual))))
            return false;
    }

    CloseSharedHandles();
    for (std::uint32_t i = 0; i < kMaxInFlight; ++i) {
        sharedSlots_[i].d3d11Color = std::move(pending[i].d3d11Color);
        sharedSlots_[i].d3d11Residual = std::move(pending[i].d3d11Residual);
        sharedSlots_[i].d3d12Color = std::move(pending[i].d3d12Color);
        sharedSlots_[i].d3d12Residual = std::move(pending[i].d3d12Residual);
        sharedSlots_[i].colorSharedHandle = pending[i].ReleaseColorHandle();
        sharedSlots_[i].residualSharedHandle =
            pending[i].ReleaseResidualHandle();
    }

    nvof_.Shutdown();
    // NVOF is optional until a real dispatch/completion backend is integrated.
    // Its fail-closed state must not disable the independent D3D11/D3D12 bridge.
    (void)nvof_.Initialize(
        d3d12Device_.Get(), d3d12Queue_.Get(), width, height);
    currentRes_ = {width, height};
    return true;
}

bool SyntheticDx11BridgeProvider::CopyInputToSlot(
    std::uint32_t slot, ID3D11Resource* gameColor) {
    if (slot >= kMaxInFlight || gameColor == nullptr || !d3d11Context_)
        return false;
    SharedSlot& target = sharedSlots_[slot];
    if (!SyntheticDx11ResourcesCopyCompatible(
            gameColor, target.d3d11Color.Get(), d3d11Device_.Get()))
        return false;
    d3d11Context_->CopyResource(target.d3d11Color.Get(), gameColor);
    return true;
}

} // namespace nrfusion
