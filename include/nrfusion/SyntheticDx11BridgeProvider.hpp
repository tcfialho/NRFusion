#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/D3D11BridgeSlotTracker.hpp"
#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/SyntheticProvider.hpp"

#include <array>
#include <mutex>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

class SyntheticDx11BridgeProvider : public ISyntheticProvider {
public:
    static constexpr std::uint32_t kMaxInFlight = D3D11BridgeSlotTracker::kCapacity;

    SyntheticDx11BridgeProvider();
    ~SyntheticDx11BridgeProvider() override;

    bool Initialize(const ProviderContext& context) override;
    void Shutdown() override;
    bool IsReady() const noexcept override { return ready_; }

    SyntheticWorkHandle Submit(
        const SyntheticFrameInputs& inputs, void* commandList) override;
    bool Poll(const SyntheticWorkHandle& handle) override;
    ResourceRef GetResidual(const SyntheticWorkHandle& handle) override;
    bool ComposeNative(const SyntheticWorkHandle& handle,
                       const ResourceRef& originalNative,
                       const ResourceRef& destinationNative,
                       void* commandList,
                       float residualWeight = 1.0f) override;

    const char* Name() const noexcept override { return "SyntheticDx11BridgeProvider"; }

    bool RecordD3D11OutputConsume(
        const SyntheticWorkHandle& handle, ID3D11DeviceContext* d3d11Context,
        ID3D11Resource* gameDestination);

    ID3D12Device* PrivateD3D12Device() const noexcept { return d3d12Device_.Get(); }
    ID3D11Device* GameD3D11Device() const noexcept { return d3d11Device_.Get(); }
    NvofMotionProvider& OpticalFlow() noexcept { return nvof_; }

private:
    struct SharedSlot {
        ComPtr<ID3D11Texture2D> d3d11Color;
        ComPtr<ID3D11Texture2D> d3d11Residual;
        HANDLE colorSharedHandle = nullptr;
        HANDLE residualSharedHandle = nullptr;
        ComPtr<ID3D12Resource> d3d12Color;
        ComPtr<ID3D12Resource> d3d12Residual;
        ComPtr<ID3D12CommandAllocator> alloc;
    };

    bool CreatePrivateD3D12();
    bool CreateSharedResources(std::uint32_t width, std::uint32_t height);
    bool CopyInputToSlot(std::uint32_t slot, ID3D11Resource* gameColor);
    void CloseSharedHandles();

    ComPtr<ID3D11Device> d3d11Device_;
    ComPtr<ID3D11DeviceContext> d3d11Context_;
    ComPtr<ID3D12Device> d3d12Device_;
    ComPtr<ID3D12CommandQueue> d3d12Queue_;
    ComPtr<ID3D12GraphicsCommandList> d3d12CmdList_;
    ComPtr<ID3D12Fence> d3d12Fence_;
    std::uint64_t nextFenceValue_ = 1;

    SyntheticDx12Provider syntheticD3D12_;
    NvofMotionProvider nvof_;
    D3D11BridgeSlotTracker slotTracker_{};
    Resolution currentRes_{};
    std::array<SharedSlot, kMaxInFlight> sharedSlots_{};
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
