#include "nrfusion/D3D11D3D12FenceBridge.hpp"

namespace nrfusion {

bool D3D11D3D12FenceBridge::CreateSharedFencePair(
    ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device,
    Microsoft::WRL::ComPtr<ID3D11Fence>& d3d11Fence,
    Microsoft::WRL::ComPtr<ID3D12Fence>& d3d12Fence) {
    if (FAILED(d3d11Device->CreateFence(
            0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d11Fence))))
        return false;

    HANDLE sharedHandle = nullptr;
    if (FAILED(d3d11Fence->CreateSharedHandle(
            nullptr, GENERIC_ALL, nullptr, &sharedHandle)))
        return false;
    const HRESULT openHr = d3d12Device->OpenSharedHandle(
        sharedHandle, IID_PPV_ARGS(&d3d12Fence));
    CloseHandle(sharedHandle);
    return SUCCEEDED(openHr);
}

bool D3D11D3D12FenceBridge::BindAfterIdle(
    ID3D11Device* d3d11Device, ID3D11DeviceContext* d3d11Context,
    ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12Queue) {
    if (!d3d11Device || !d3d11Context || !d3d12Device || !d3d12Queue)
        return false;
    ResetAfterIdle();

    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    d3d11Context->GetDevice(&contextDevice);
    if (contextDevice.Get() != d3d11Device) return false;

    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    if (FAILED(d3d12Queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
        queueDevice.Get() != d3d12Device)
        return false;

    Microsoft::WRL::ComPtr<ID3D11Device5> device5;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
    if (FAILED(d3d11Device->QueryInterface(IID_PPV_ARGS(&device5))) ||
        FAILED(d3d11Context->QueryInterface(IID_PPV_ARGS(&context4))))
        return false;

    if (!CreateSharedFencePair(
            device5.Get(), d3d12Device, inputFence11_, inputFence12_) ||
        !CreateSharedFencePair(
            device5.Get(), d3d12Device, outputFence11_, outputFence12_)) {
        ResetAfterIdle();
        return false;
    }

    d3d11Context_ = std::move(context4);
    d3d12Queue_ = d3d12Queue;
    return true;
}

void D3D11D3D12FenceBridge::ResetAfterIdle() noexcept {
    outputFence12_.Reset();
    outputFence11_.Reset();
    inputFence12_.Reset();
    inputFence11_.Reset();
    d3d12Queue_.Reset();
    d3d11Context_.Reset();
    nextInputValue_ = 1;
    nextOutputValue_ = 1;
}

bool D3D11D3D12FenceBridge::QueueInputHandoff() noexcept {
    if (!d3d11Context_ || !d3d12Queue_ || !inputFence11_ || !inputFence12_)
        return false;
    const std::uint64_t value = nextInputValue_++;
    return SUCCEEDED(d3d11Context_->Signal(inputFence11_.Get(), value)) &&
        SUCCEEDED(d3d12Queue_->Wait(inputFence12_.Get(), value));
}

bool D3D11D3D12FenceBridge::QueueOutputHandoff() noexcept {
    if (!d3d11Context_ || !d3d12Queue_ || !outputFence11_ || !outputFence12_)
        return false;
    const std::uint64_t value = nextOutputValue_++;
    return SUCCEEDED(d3d12Queue_->Signal(outputFence12_.Get(), value)) &&
        SUCCEEDED(d3d11Context_->Wait(outputFence11_.Get(), value));
}

} // namespace nrfusion
