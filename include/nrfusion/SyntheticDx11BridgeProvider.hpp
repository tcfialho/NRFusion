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

#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/SyntheticProvider.hpp"

#include <array>
#include <mutex>
#include <optional>

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
        ID3D11Device* d3d11Device, ID3D11DeviceContext* d3d11Context,
        ID3D12Device* d3d12Device, ID3D12CommandQueue* d3d12Queue);
    void ResetAfterIdle() noexcept;
    bool QueueInputHandoff() noexcept;
    bool QueueOutputHandoff() noexcept;

private:
    bool CreateSharedFencePair(
        ID3D11Device5* d3d11Device, ID3D12Device* d3d12Device,
        ComPtr<ID3D11Fence>& d3d11Fence,
        ComPtr<ID3D12Fence>& d3d12Fence);
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
