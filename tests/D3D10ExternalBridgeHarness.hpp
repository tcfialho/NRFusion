#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/D3D10CarrierRoute.hpp"
#include "nrfusion/D3D11D3D12FenceBridge.hpp"

#include <cstdint>
#include <vector>

namespace nrfusion::test {

using Microsoft::WRL::ComPtr;

struct D3D10BridgeCycleResources {
    ComPtr<ID3D10Texture2D> source10;
    ComPtr<ID3D10Texture2D> destination10;
    ComPtr<ID3D10Texture2D> readback10;
    ComPtr<ID3D10Texture2D> legacyInput10;
    ComPtr<ID3D10Texture2D> legacyOutput10;
    ComPtr<IDXGIKeyedMutex> inputMutex10;
    ComPtr<IDXGIKeyedMutex> outputMutex10;

    ComPtr<ID3D11Texture2D> legacyInput11;
    ComPtr<ID3D11Texture2D> legacyOutput11;
    ComPtr<IDXGIKeyedMutex> inputMutex11;
    ComPtr<IDXGIKeyedMutex> outputMutex11;
    ComPtr<ID3D11Texture2D> ntShared11;
    ComPtr<IDXGIKeyedMutex> ntMutex11;
    ComPtr<ID3D12Resource> ntShared12;

    std::vector<std::uint16_t> expected;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class D3D10ExternalBridgeHarness {
public:
    bool Open();
    void Close() noexcept;

    bool RunReuseCycles(std::uint32_t count);
    bool RunRecreationCycles(std::uint32_t count);
    std::uint64_t CarrierCopyCount() const noexcept {
        return carrierCopies_;
    }

private:
    bool CreateDevices();
    bool CreateResources(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t seed,
        D3D10BridgeCycleResources& resources);
    bool ExecuteRoundTrip(D3D10BridgeCycleResources& resources);
    bool VerifyRoundTrip(D3D10BridgeCycleResources& resources);
    bool RouteContractMatches() const noexcept;

    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D10Device1> device10_;
    ComPtr<ID3D11Device> device11_;
    ComPtr<ID3D11DeviceContext> context11_;
    ComPtr<ID3D12Device> device12_;
    ComPtr<ID3D12CommandQueue> queue12_;
    D3D11D3D12FenceBridge fenceBridge_{};
    std::uint64_t carrierCopies_ = 0;
};

} // namespace nrfusion::test
