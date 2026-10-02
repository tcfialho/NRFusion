#pragma once

#include "nrfusion/NrTimingSource.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <d3d12.h>
#include <wrl/client.h>

namespace nrfusion {

class D3D12RetiredTimingSource {
public:
    static constexpr std::size_t kCapacity = 8;

    bool BindAfterIdle(ID3D12Device* device, ID3D12CommandQueue* queue);
    void ResetAfterIdle() noexcept;

    bool Begin(ID3D12GraphicsCommandList* commands, const WorkTicket& ticket) noexcept;
    bool BeginInvalid(ID3D12GraphicsCommandList* commands) noexcept;
    bool End(ID3D12GraphicsCommandList* commands, std::uint64_t completionValue) noexcept;
    bool EndInvalid(ID3D12GraphicsCommandList* commands,
                    std::uint64_t completionValue) noexcept;
    std::optional<NrRetiredTimingSample> TryRetire(
        std::uint64_t completedValue) noexcept;

    std::size_t Size() const noexcept { return size_; }
    std::uint64_t TimestampFrequency() const noexcept { return timestampFrequency_; }

private:
    struct Entry {
        NrRetiredTimingSample sample{};
        std::uint64_t completionValue = 0;
        bool pending = false;
    };

    bool BeginSample(
        ID3D12GraphicsCommandList* commands, NrRetiredTimingSample sample) noexcept;
    bool BelongsToBoundDevice(ID3D12DeviceChild* child) const noexcept;
    void PopFront() noexcept;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    std::array<Entry, kCapacity> entries_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t recordingSlot_ = 0;
    ID3D12GraphicsCommandList* recordingCommands_ = nullptr;
    std::uint64_t timestampFrequency_ = 0;
    std::uint64_t lastCompletionValue_ = 0;
};

static_assert(NrTimingSource<D3D12RetiredTimingSource>);

}
