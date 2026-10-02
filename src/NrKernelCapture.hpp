#pragma once
#include "NrKernelAbiDiscovery.hpp"
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace nrfusion::kernelprofile {
struct CaptureRange {
    std::string name;
    unsigned parameterOffset = 0;
    std::uint64_t bytes = 0;
};
struct CaptureSlice {
    Microsoft::WRL::ComPtr<ID3D12Resource> source;
    std::uint64_t offset = 0, bytes = 0, readbackOffset = 0;
    std::uint32_t state = 0;
    std::string name;
};
struct KernelCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    std::array<CaptureSlice, 7> slices{};
    std::array<std::uint8_t, 72> parameters{};
    LaunchRecord launch{};
    bool before = false, after = false, written = false;
};
struct CaptureState {
    std::mutex mutex;
    std::filesystem::path directory;
    std::string name, moduleHash;
    std::array<CaptureRange, 5> ranges{};
    std::array<unsigned, 3> grid{}, block{};
    unsigned width = 0, height = 0, shared = 0;
    std::array<KernelCapture, 3> captures{};
    ModuleImage image;
    std::uint64_t lastFrame = UINT64_MAX;
    unsigned count = 0, active = UINT32_MAX;
    std::atomic<bool> enabled{false};
    bool failed = false;
};
CaptureState& Captures();
void ConfigureCapture();
void SelectCaptureModule(const FunctionIdentity& identity, ModuleImage image);
bool PrepareCaptureFrame(ID3D12Device* device);
void CaptureBefore(ID3D12GraphicsCommandList* commands, const LaunchRecord& record, const void* parameters);
void CaptureAfter(ID3D12GraphicsCommandList* commands, bool successful);
bool RetireCaptures(ID3D12CommandQueue* queue);
bool WriteCapturePackage(CaptureState& state, KernelCapture& capture, unsigned index,
                         ID3D12CommandQueue* queue);
}
