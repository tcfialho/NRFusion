#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d11_3.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <string>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

class SyntheticDx11BridgeProvider : public ISyntheticProvider {
public:
    static constexpr uint32_t kMaxInFlight = 3;

    SyntheticDx11BridgeProvider();
    ~SyntheticDx11BridgeProvider() override;

    bool Initialize(const ProviderContext& context) override;
    void Shutdown() override;
    bool IsReady() const noexcept override { return ready_; }

    SyntheticWorkHandle Submit(const SyntheticFrameInputs& inputs, void* commandList) override;
    bool Poll(const SyntheticWorkHandle& handle) override;
    ResourceRef GetResidual(const SyntheticWorkHandle& handle) override;
    bool ComposeNative(const SyntheticWorkHandle& handle,
                       const ResourceRef& originalNative,
                       const ResourceRef& destinationNative,
                       void* commandList,
                       float residualWeight = 1.0f) override;

    const char* Name() const noexcept override { return "SyntheticDx11BridgeProvider"; }

    // D3D11 Bridge synchronization: GPU copies Color/Depth/Motion into shared textures
    bool RecordD3D11InputCopy(ID3D11DeviceContext* d3d11Context,
                             ID3D11Resource* gameColor,
                             ID3D11Resource* gameDepth,
                             ID3D11Resource* gameMotion);

    // D3D11 Bridge synchronization: GPU copies shared output/residual back into game buffer
    bool RecordD3D11OutputConsume(ID3D11DeviceContext* d3d11Context,
                                 ID3D11Resource* gameDestination);

    ID3D12Device* PrivateD3D12Device() const noexcept { return d3d12Device_.Get(); }
    ID3D11Device* GameD3D11Device() const noexcept { return d3d11Device_.Get(); }
    NvofMotionProvider& OpticalFlow() noexcept { return nvof_; }

private:
    struct SharedSlot {
        uint64_t workId = 0;
        uint64_t producerFenceValue = 0;
        uint64_t consumerFenceValue = 0;

        // D3D11 side
        ComPtr<ID3D11Texture2D> d3d11Color;
        ComPtr<ID3D11Texture2D> d3d11Depth;
        ComPtr<ID3D11Texture2D> d3d11Motion;
        ComPtr<ID3D11Texture2D> d3d11Residual;
        HANDLE colorSharedHandle = nullptr;
        HANDLE residualSharedHandle = nullptr;

        // D3D12 private side
        ComPtr<ID3D12Resource> d3d12Color;
        ComPtr<ID3D12Resource> d3d12Residual;
        ComPtr<ID3D12CommandAllocator> alloc;

        bool inUse = false;
    };

    bool CreatePrivateD3D12();
    bool CreateSharedResources(uint32_t width, uint32_t height);
    void CloseSharedHandles();

    ComPtr<ID3D11Device> d3d11Device_;
    ComPtr<ID3D11DeviceContext> d3d11Context_;

    ComPtr<ID3D12Device> d3d12Device_;
    ComPtr<ID3D12CommandQueue> d3d12Queue_;
    ComPtr<ID3D12CommandAllocator> d3d12Alloc_;
    ComPtr<ID3D12GraphicsCommandList> d3d12CmdList_;
    ComPtr<ID3D12Fence> d3d12Fence_;
    uint64_t nextFenceValue_ = 1;

    SyntheticDx12Provider syntheticD3D12_;
    NvofMotionProvider nvof_;

    Resolution currentRes_{};
    std::array<SharedSlot, kMaxInFlight> sharedSlots_{};
    uint32_t currentSlot_ = 0;
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
