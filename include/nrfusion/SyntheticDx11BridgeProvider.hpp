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
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/SyntheticProvider.hpp"

#include <array>
#include <mutex>
#include <optional>
#include <utility>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

bool SyntheticDx11ResourcesCopyCompatible(
    ID3D11Resource* source, ID3D11Resource* destination,
    ID3D11Device* expectedDevice) noexcept;

struct SyntheticDx11SlotLease {
    std::uint32_t slot = 0;
    std::uint64_t workId = 0;
};

class SyntheticDx11SlotTracker {
public:
    static constexpr std::uint32_t kCapacity = 3;

    std::optional<SyntheticDx11SlotLease> Acquire(
        std::uint64_t workId, std::uint64_t completedFence) noexcept {
        if (workId == 0 || Find(workId)) return std::nullopt;
        for (std::uint32_t offset = 0; offset < kCapacity; ++offset) {
            const std::uint32_t slot = (next_ + offset) % kCapacity;
            Entry& entry = entries_[slot];
            const bool retired = entry.occupied && entry.fenceValue != 0 &&
                entry.fenceValue <= completedFence;
            if (entry.occupied && !retired) continue;
            entry = {workId, 0, true};
            next_ = (slot + 1) % kCapacity;
            return SyntheticDx11SlotLease{slot, workId};
        }
        return std::nullopt;
    }

    bool MarkSubmitted(
        const SyntheticDx11SlotLease& lease,
        std::uint64_t fenceValue) noexcept {
        if (lease.slot >= kCapacity || lease.workId == 0 || fenceValue == 0)
            return false;
        Entry& entry = entries_[lease.slot];
        if (!entry.occupied || entry.workId != lease.workId ||
            entry.fenceValue != 0)
            return false;
        entry.fenceValue = fenceValue;
        return true;
    }

    bool Release(const SyntheticDx11SlotLease& lease) noexcept {
        if (lease.slot >= kCapacity || lease.workId == 0) return false;
        Entry& entry = entries_[lease.slot];
        if (!entry.occupied || entry.workId != lease.workId ||
            entry.fenceValue != 0)
            return false;
        entry = {};
        return true;
    }

    std::optional<std::uint32_t> Find(std::uint64_t workId) const noexcept {
        if (workId == 0) return std::nullopt;
        for (std::uint32_t slot = 0; slot < kCapacity; ++slot)
            if (entries_[slot].occupied && entries_[slot].workId == workId)
                return slot;
        return std::nullopt;
    }

    bool AllRetired(std::uint64_t completedFence) const noexcept {
        for (const Entry& entry : entries_)
            if (entry.occupied &&
                (entry.fenceValue == 0 || entry.fenceValue > completedFence))
                return false;
        return true;
    }

    void Reset() noexcept {
        for (Entry& entry : entries_) entry = {};
        next_ = 0;
    }

private:
    struct Entry {
        std::uint64_t workId = 0;
        std::uint64_t fenceValue = 0;
        bool occupied = false;
    };
    std::array<Entry, kCapacity> entries_{};
    std::uint32_t next_ = 0;
};

class SyntheticDx11FenceBridge {
public:
    bool BindAfterIdle(
        ID3D11Device* device11, ID3D11DeviceContext* context11,
        ID3D12Device* device12, ID3D12CommandQueue* queue12) {
        if (!device11 || !context11 || !device12 || !queue12) return false;
        ResetAfterIdle();
        ComPtr<ID3D11Device> contextDevice;
        context11->GetDevice(&contextDevice);
        if (contextDevice.Get() != device11) return false;
        ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue12->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
            queueDevice.Get() != device12)
            return false;
        ComPtr<ID3D11Device5> device5;
        ComPtr<ID3D11DeviceContext4> context4;
        if (FAILED(device11->QueryInterface(IID_PPV_ARGS(&device5))) ||
            FAILED(context11->QueryInterface(IID_PPV_ARGS(&context4))))
            return false;
        if (!CreateSharedFencePair(device5.Get(), device12, inputFence11_, inputFence12_) ||
            !CreateSharedFencePair(device5.Get(), device12, outputFence11_, outputFence12_)) {
            ResetAfterIdle();
            return false;
        }
        d3d11Context_ = std::move(context4);
        d3d12Queue_ = queue12;
        return true;
    }

    void ResetAfterIdle() noexcept {
        outputFence12_.Reset(); outputFence11_.Reset();
        inputFence12_.Reset(); inputFence11_.Reset();
        d3d12Queue_.Reset(); d3d11Context_.Reset();
        nextInputValue_ = 1; nextOutputValue_ = 1;
    }

    bool QueueInputHandoff() noexcept {
        if (!d3d11Context_ || !d3d12Queue_ || !inputFence11_ || !inputFence12_)
            return false;
        const std::uint64_t value = nextInputValue_++;
        return SUCCEEDED(d3d11Context_->Signal(inputFence11_.Get(), value)) &&
            SUCCEEDED(d3d12Queue_->Wait(inputFence12_.Get(), value));
    }

    bool QueueOutputHandoff() noexcept {
        if (!d3d11Context_ || !d3d12Queue_ || !outputFence11_ || !outputFence12_)
            return false;
        const std::uint64_t value = nextOutputValue_++;
        return SUCCEEDED(d3d12Queue_->Signal(outputFence12_.Get(), value)) &&
            SUCCEEDED(d3d11Context_->Wait(outputFence11_.Get(), value));
    }

private:
    bool CreateSharedFencePair(
        ID3D11Device5* device11, ID3D12Device* device12,
        ComPtr<ID3D11Fence>& fence11, ComPtr<ID3D12Fence>& fence12) {
        if (FAILED(device11->CreateFence(
                0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence11))))
            return false;
        HANDLE handle = nullptr;
        if (FAILED(fence11->CreateSharedHandle(
                nullptr, GENERIC_ALL, nullptr, &handle)))
            return false;
        const HRESULT opened =
            device12->OpenSharedHandle(handle, IID_PPV_ARGS(&fence12));
        CloseHandle(handle);
        return SUCCEEDED(opened);
    }

    ComPtr<ID3D11DeviceContext4> d3d11Context_;
    ComPtr<ID3D12CommandQueue> d3d12Queue_;
    ComPtr<ID3D11Fence> inputFence11_;
    ComPtr<ID3D12Fence> inputFence12_;
    ComPtr<ID3D11Fence> outputFence11_;
    ComPtr<ID3D12Fence> outputFence12_;
    std::uint64_t nextInputValue_ = 1;
    std::uint64_t nextOutputValue_ = 1;
};

class SyntheticDx11BridgeProvider : public ISyntheticProvider {
public:
    static constexpr std::uint32_t kMaxInFlight = SyntheticDx11SlotTracker::kCapacity;

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
    SyntheticDx11SlotTracker slotTracker_{};
    SyntheticDx11FenceBridge sync_{};
    Resolution currentRes_{};
    std::array<SharedSlot, kMaxInFlight> sharedSlots_{};
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
