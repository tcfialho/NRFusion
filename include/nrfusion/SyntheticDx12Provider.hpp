#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/SyntheticDlaaContract.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

class SyntheticDx12Provider : public ISyntheticProvider {
public:
    static constexpr uint32_t kRingSlots = 3;

    SyntheticDx12Provider();
    ~SyntheticDx12Provider() override;

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

    const char* Name() const noexcept override { return "SyntheticDx12Provider"; }

    // Computes lowResidual = lowNeuralOut - lowColor at working resolution (CSExtractResidual,
    // compiled since EnsureShaders() but previously never dispatched by this class). The caller is
    // responsible for having already written a real answer into the slot's lowNeuralOut -- Submit()
    // only prepares lowColor; it does not run any model.
    bool ExtractResidual(const SyntheticWorkHandle& handle, void* commandList);

    // Helpers for direct access in test harnesses and benchmarks
    ID3D12Resource* GetSlotLowColor(uint32_t slot) const;
    ID3D12Resource* GetSlotLowResidual(uint32_t slot) const;
    ID3D12Resource* GetSlotLowNeuralOut(uint32_t slot) const;
    // Ring slot index actively holding this handle's resources, or kRingSlots if none matches
    // (the ticket was never submitted here, or its slot has since been recycled).
    uint32_t SlotForWork(uint64_t workId) const;
    uint32_t NextSlotForSubmit() const noexcept {
        return currentSlot_;
    }
    uint64_t CompletedFenceValue() const;

private:
    struct Slot {
        uint64_t activeWorkId = 0;
        uint64_t fenceValue = 0;
        Resolution resolution{};
        ComPtr<ID3D12Resource> lowColor;
        ComPtr<ID3D12Resource> lowDepth;
        ComPtr<ID3D12Resource> lowMotion;
        ComPtr<ID3D12Resource> lowNeuralOut;
        ComPtr<ID3D12Resource> lowResidual;
        bool inUse = false;
    };

    bool EnsureShaders();
    bool EnsureSlotResources(uint32_t slot, Resolution workRes);
    void Transition(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    uint64_t nextFenceValue_ = 1;

    // Compute root signature and PSOs
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> downsamplePso_;
    ComPtr<ID3D12PipelineState> extractResidualPso_;
    ComPtr<ID3D12PipelineState> composeResidualPso_;
    ComPtr<ID3D12DescriptorHeap> srvUavHeap_;
    UINT descriptorSize_ = 0;

    std::array<Slot, kRingSlots> ringSlots_{};
    uint32_t currentSlot_ = 0;
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
