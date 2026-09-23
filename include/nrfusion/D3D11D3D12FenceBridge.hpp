#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace nrfusion {

class D3D11D3D12FenceBridge {
public:
    bool BindAfterIdle(
        ID3D11Device* d3d11Device, ID3D11DeviceContext* d3d11Context,
        ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12Queue);
    void ResetAfterIdle() noexcept;

    bool QueueInputHandoff() noexcept;
    bool QueueOutputHandoff() noexcept;

private:
    bool CreateSharedFencePair(
        ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device,
        Microsoft::WRL::ComPtr<ID3D11Fence>& d3d11Fence,
        Microsoft::WRL::ComPtr<ID3D12Fence>& d3d12Fence);

    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> d3d11Context_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> d3d12Queue_;
    Microsoft::WRL::ComPtr<ID3D11Fence> inputFence11_;
    Microsoft::WRL::ComPtr<ID3D12Fence> inputFence12_;
    Microsoft::WRL::ComPtr<ID3D11Fence> outputFence11_;
    Microsoft::WRL::ComPtr<ID3D12Fence> outputFence12_;
    std::uint64_t nextInputValue_ = 1;
    std::uint64_t nextOutputValue_ = 1;
};

} // namespace nrfusion
