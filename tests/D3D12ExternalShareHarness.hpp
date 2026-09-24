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

#include <cstdint>

namespace nrfusion::test {

class D3D12ExternalShareHarness {
public:
    D3D12ExternalShareHarness() = default;
    ~D3D12ExternalShareHarness();

    D3D12ExternalShareHarness(const D3D12ExternalShareHarness&) = delete;
    D3D12ExternalShareHarness& operator=(
        const D3D12ExternalShareHarness&) = delete;

    bool Open(bool hardwareRequired);
    void Close() noexcept;

    LUID AdapterLuid() const noexcept { return adapterLuid_; }
    HANDLE ColorHandle() const noexcept { return colorHandle_; }
    HANDLE OutputHandle() const noexcept { return outputHandle_; }
    HANDLE ProducerFenceHandle() const noexcept {
        return producerFenceHandle_;
    }
    HANDLE ConsumerFenceHandle() const noexcept {
        return consumerFenceHandle_;
    }
    std::uint64_t AllocationSize() const noexcept {
        return allocationSize_;
    }

    bool SignalProducer(std::uint64_t value) noexcept;
    bool WaitConsumer(
        std::uint64_t value, std::uint32_t timeoutMs) noexcept;

private:
    bool CreateSharedTexture(
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        HANDLE& sharedHandle);

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Resource> color_;
    Microsoft::WRL::ComPtr<ID3D12Resource> output_;
    Microsoft::WRL::ComPtr<ID3D12Fence> producerFence_;
    Microsoft::WRL::ComPtr<ID3D12Fence> consumerFence_;

    HANDLE colorHandle_ = nullptr;
    HANDLE outputHandle_ = nullptr;
    HANDLE producerFenceHandle_ = nullptr;
    HANDLE consumerFenceHandle_ = nullptr;
    LUID adapterLuid_{};
    std::uint64_t allocationSize_ = 0;
};

} // namespace nrfusion::test
