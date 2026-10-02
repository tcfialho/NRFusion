#pragma once
#include "NrKernelAbiDiscovery.hpp"
#include "nrfusion/NrSwinCaptureContract.hpp"
#include <wrl/client.h>
#include <filesystem>
#include <mutex>
#include <atomic>

namespace nrfusion::kernelprofile {
inline constexpr auto& SwinCaptureName = neuralswin::Name;
inline constexpr auto& SwinCaptureModuleHash = neuralswin::ModuleHash;
inline constexpr auto& SwinPointerOffsets = neuralswin::PointerOffsets;
inline constexpr auto& SwinPointerParents = neuralswin::PointerParents;
inline constexpr auto& SwinParentBytes = neuralswin::ParentBytes;
struct SwinParentCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> source;
    std::uint64_t base = 0;
    std::uint32_t state = 0;
};
struct SwinKernelCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    std::array<SwinParentCapture, 2> parents{};
    std::array<std::uint8_t, 88> parameters{};
    std::array<std::uint64_t, 5> offsets{};
    LaunchRecord launch{};
    bool complete = false, written = false;
};
struct SwinCaptureState {
    std::mutex mutex;
    std::filesystem::path directory;
    ModuleImage image;
    std::array<SwinKernelCapture, 3> captures{};
    std::uint64_t lastFrame = UINT64_MAX;
    unsigned count = 0, active = UINT32_MAX;
    std::atomic<bool> enabled{false};
    bool failed = false;
};
SwinCaptureState& SwinCaptures();
inline bool IsSwinCaptureActive() noexcept {
    return SwinCaptures().enabled.load(std::memory_order_relaxed);
}
void ConfigureSwinCapture();
void SelectSwinCaptureModule(const FunctionIdentity&, ModuleImage);
bool PrepareSwinCaptureFrame(ID3D12Device*);
void CaptureSwinBefore(ID3D12GraphicsCommandList*, const LaunchRecord&, const void*);
void CaptureSwinAfter(ID3D12GraphicsCommandList*, bool);
bool RetireSwinCaptures(ID3D12CommandQueue*);
bool WriteSwinCapturePackage(SwinCaptureState&, SwinKernelCapture&, unsigned, ID3D12CommandQueue*);
} // namespace nrfusion::kernelprofile
