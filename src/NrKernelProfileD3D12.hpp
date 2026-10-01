#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>

namespace nrfusion::kernelprofile {

struct FunctionIdentity {
    std::uintptr_t function = 0;
    std::uintptr_t module = 0;
    std::uintptr_t device = 0;
    std::uint64_t generation = 0;
    std::array<char, 512> name{};
    std::array<char, 65> moduleHash{};
    bool truncated = false;
};

struct LaunchRecord {
    FunctionIdentity identity{};
    std::uintptr_t commands = 0;
    std::array<std::uint32_t, 3> grid{}, block{};
    std::uint32_t sharedBytes = 0;
    std::uint32_t parameterBytes = 0;
    std::uint32_t query = 0;
    std::uint32_t chainCount = 0;
    std::uint32_t chainIndex = 0;
    std::uint64_t frame = 0;
    std::uint64_t sequence = 0;
    bool successful = false;
};

void EnableNvapiObservation(bool enabled) noexcept;
bool NvapiObservationEnabled() noexcept;
void* InterceptNvapiInterface(std::uint32_t id, void* original) noexcept;
bool BeginFrame(ID3D12Device* device, std::uint64_t frame, const char* csvPath);
void BeginNeuralPass(ID3D12GraphicsCommandList* commands) noexcept;
void EndNeuralPass() noexcept;
bool RetireFrame(ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t completion,
                 std::uint64_t& kernels, std::uint64_t& chains);
unsigned BeginChain(ID3D12GraphicsCommandList* commands, unsigned count) noexcept;
void StartChainTimer(ID3D12GraphicsCommandList* commands, unsigned query) noexcept;
void ObserveLaunch(unsigned query, const LaunchRecord& record) noexcept;
void EndChain(ID3D12GraphicsCommandList* commands, unsigned query, bool successful) noexcept;
bool WriteFrame(const char* path, const LaunchRecord* records, std::size_t count,
                 const std::uint64_t* ticks, std::uint64_t frequency,
                 std::uintptr_t queue, std::uint64_t dropped);

} // namespace nrfusion::kernelprofile
