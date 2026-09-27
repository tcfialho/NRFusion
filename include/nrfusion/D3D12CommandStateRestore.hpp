#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <array>
#include <cstdint>

namespace nrfusion {

using D3D12RestoreAdditionalStateFn =
    void (*)(ID3D12GraphicsCommandList*, void*) noexcept;

struct D3D12CommandStateRestore {
    bool restoreDescriptorHeaps = false;
    bool restoreGraphicsRootSignature = false;
    bool restoreComputeRootSignature = false;
    bool restorePipelineState = false;

    std::array<ID3D12DescriptorHeap*, 2> descriptorHeaps{};
    std::uint32_t descriptorHeapCount = 0;
    ID3D12RootSignature* graphicsRootSignature = nullptr;
    ID3D12RootSignature* computeRootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;

    D3D12RestoreAdditionalStateFn restoreAdditionalState = nullptr;
    void* additionalStateContext = nullptr;

    bool Valid() const noexcept {
        if (restoreDescriptorHeaps) {
            if (descriptorHeapCount > descriptorHeaps.size()) return false;
            for (std::uint32_t i = 0; i < descriptorHeapCount; ++i)
                if (descriptorHeaps[i] == nullptr) return false;
        }
        if (restoreGraphicsRootSignature && graphicsRootSignature == nullptr)
            return false;
        if (restoreComputeRootSignature && computeRootSignature == nullptr)
            return false;
        if (restorePipelineState && pipelineState == nullptr)
            return false;
        return true;
    }

    bool Apply(ID3D12GraphicsCommandList* commandList) const noexcept {
        if (commandList == nullptr || !Valid()) return false;
        if (restoreDescriptorHeaps) {
            commandList->SetDescriptorHeaps(
                descriptorHeapCount,
                descriptorHeapCount == 0 ? nullptr : descriptorHeaps.data());
        }
        if (restoreGraphicsRootSignature)
            commandList->SetGraphicsRootSignature(graphicsRootSignature);
        if (restoreComputeRootSignature)
            commandList->SetComputeRootSignature(computeRootSignature);
        if (restorePipelineState)
            commandList->SetPipelineState(pipelineState);
        if (restoreAdditionalState != nullptr)
            restoreAdditionalState(commandList, additionalStateContext);
        return true;
    }
};

} // namespace nrfusion
