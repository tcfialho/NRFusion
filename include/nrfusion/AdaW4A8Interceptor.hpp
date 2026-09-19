#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace nrfusion {

// Detects if the D3D12 device is an NVIDIA Ada Lovelace SM89 (RTX 40 series) GPU.
// Returns false for RTX 50 (Blackwell) to ensure Blackwell uses its existing default path untouched.
bool IsAdaSm89Architecture(ID3D12Device* device) noexcept;

// Initializes the Ada SM89 W4A8 assets (weights_sm89.bin and w4a8_ffn_sm89.cubin).
// Returns true if assets are verified and ready.
bool InitializeAdaW4A8(ID3D12Device* device) noexcept;

// Attempts to intercept the current kernel launch on the Direct3D 12 command list.
// If the launch matches our supported FFN and the hardware is Ada SM89:
//   dispatches the custom SM89 W4A8 + FP8 correction kernel and returns true.
// Otherwise (Blackwell RTX 50, other blocks, unsupported shape, or fallback):
//   returns false, allowing the original runtime to handle execution unmodified.
bool TryInterceptAdaW4A8(ID3D12GraphicsCommandList* cmdList, const void* launchParams, std::uint32_t count) noexcept;

// NVAPI names every kernel at creation. Matching on that instead of on a parameter-block size
// removes an ambiguity that silently matched a full-frame kernel, and survives a resolution change.
void NoteAdaW4A8Kernel(void* function, const char* name) noexcept;

struct AdaW4A8Params;

// The kernel runs as a cubin on the game's own D3D12 command list, which only the host that owns
// the NVAPI entry points can launch; without one installed the path stays inert instead of
// handing device-side code a set of host pointers.
using AdaW4A8CubinLauncher = bool (*)(ID3D12GraphicsCommandList* cmdList, const AdaW4A8Params& params) noexcept;
void SetAdaW4A8CubinLauncher(AdaW4A8CubinLauncher launcher) noexcept;
bool HasAdaW4A8CubinLauncher() noexcept;

// True only once a launch has actually been replaced. Hardware match and asset presence are not
// acceleration, and reporting them as such is what made the menu look alive while nothing ran.
bool IsAdaW4A8Accelerating() noexcept;
std::uint64_t AdaW4A8ReplacedLaunches() noexcept;

// Reports status of the Ada SM89 W4A8 execution path.
const char* GetAdaW4A8Status() noexcept;

// In-game UI runtime controls:
void SetAdaW4A8Enabled(bool enabled) noexcept;
bool IsAdaW4A8Enabled() noexcept;

void SetAdaW4A8Backend(const char* backendName) noexcept;
const char* GetAdaW4A8Backend() noexcept;

void RecalibrateAdaW4A8Backend() noexcept;

bool IsBlackwellArchitecture() noexcept;
const char* GetGpuArchitectureName() noexcept;

} // namespace nrfusion
