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

constexpr std::size_t kGameD3D12MaxRootParameters = 64;
constexpr std::size_t kGameD3D12MaxRootConstants = 64;

enum class GameD3D12RootArgType : std::uint8_t {
    None,
    DescriptorTable,
    Constants,
    ConstantBufferView,
    ShaderResourceView,
    UnorderedAccessView,
};

struct GameD3D12RootArgSnapshot {
    GameD3D12RootArgType type = GameD3D12RootArgType::None;
    D3D12_GPU_DESCRIPTOR_HANDLE table{};
    D3D12_GPU_VIRTUAL_ADDRESS address = 0;
    std::array<std::uint32_t, kGameD3D12MaxRootConstants> constants{};
    std::uint64_t constantsMask = 0;
};

struct GameD3D12RootSnapshot {
    ID3D12RootSignature* signature = nullptr;
    std::array<GameD3D12RootArgSnapshot, kGameD3D12MaxRootParameters> args{};
};

struct GameD3D12CommandStateSnapshot {
    std::array<ID3D12DescriptorHeap*, 2> descriptorHeaps{};
    UINT descriptorHeapCount = 0;
    ID3D12PipelineState* pipelineState = nullptr;
    GameD3D12RootSnapshot compute{};
    GameD3D12RootSnapshot graphics{};
};

bool InstallGameD3D12CommandStateTracking(ID3D12GraphicsCommandList* commandList) noexcept;
bool SnapshotGameD3D12CommandState(
    ID3D12GraphicsCommandList* commandList,
    GameD3D12CommandStateSnapshot& snapshot) noexcept;
bool RestoreGameD3D12CommandState(
    ID3D12GraphicsCommandList* commandList,
    const GameD3D12CommandStateSnapshot& snapshot) noexcept;

class ScopedGameD3D12CommandStateSuppression {
public:
    ScopedGameD3D12CommandStateSuppression() noexcept;
    ~ScopedGameD3D12CommandStateSuppression();
    ScopedGameD3D12CommandStateSuppression(const ScopedGameD3D12CommandStateSuppression&) = delete;
    ScopedGameD3D12CommandStateSuppression& operator=(const ScopedGameD3D12CommandStateSuppression&) = delete;
};

} // namespace nrfusion
