#pragma once
#include "NrKernelAbiDiscovery.hpp"
#include "nrfusion/NrKernelChainContract.hpp"
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace nrfusion::kernelprofile {
struct ChainRegion {
    Microsoft::WRL::ComPtr<ID3D12Resource> source;
    std::uint64_t address = 0, sourceOffset = 0, beforeOffset = 0, afterOffset = 0;
    std::uint32_t state = 0;
};
struct ChainCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    std::array<ChainRegion, 16> regions{};
    std::array<std::array<std::uint8_t, 72>, 3> parameters{};
    std::array<LaunchRecord, 3> launches{};
    std::array<std::uint64_t, 4> boundaryOffsets{};
    bool complete = false, written = false;
};
struct ChainCaptureState {
    std::mutex mutex;
    std::atomic<bool> enabled{false};
    std::filesystem::path directory;
    ModuleImage image;
    std::array<ChainCapture, 3> captures{};
    unsigned count = 0, stage = 0;
    bool active = false, armed = false, failed = false;
    std::uint64_t lastFrame = UINT64_MAX;
};
ChainCaptureState& ChainCaptures();
void ConfigureChainCapture();
void SelectChainCaptureModule(const FunctionIdentity&, ModuleImage);
bool PrepareChainCaptureFrame(ID3D12Device*);
void CaptureChainBefore(ID3D12GraphicsCommandList*, const LaunchRecord&, const void*);
void CaptureChainAfter(ID3D12GraphicsCommandList*, bool);
bool RetireChainCaptures(ID3D12CommandQueue*);
bool WriteChainCapture(ChainCaptureState&, ChainCapture&, unsigned, ID3D12CommandQueue*);
}
