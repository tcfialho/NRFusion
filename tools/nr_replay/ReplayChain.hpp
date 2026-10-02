#pragma once
#include "Replay.hpp"
#include "nrfusion/NrKernelChainContract.hpp"

namespace nrreplay {
struct ChainResource {
    std::uint64_t bytes = 0, originalBase = 0;
};
struct ChainRegion {
    unsigned resource = 0;
    std::uint64_t offset = 0, originalAddress = 0;
    Bytes before, after;
};
struct ChainPacket {
    Bytes image;
    std::vector<ChainResource> resources;
    std::array<ChainRegion, 16> regions;
    std::array<std::array<std::uint8_t, 72>, 3> parameters;
};
struct ChainExecution {
    GpuContext context;
    std::vector<ComPtr<ID3D12Resource>> resources;
    ComPtr<ID3D12Resource> upload;
    std::array<std::uint64_t, 16> uploadOffsets{};
    std::array<std::array<std::uint8_t, 72>, 3> parameters;
    std::array<NVAPI_CU_KERNEL_LAUNCH_PARAMS, 3> kernels{};
    decltype(&NvAPI_D3D12_LaunchCuKernelChain) launch = nullptr;
    const ChainPacket* packet = nullptr;
    bool batched = false;
};
ComPtr<ID3D12Resource> ChainBuffer(GpuContext&, std::uint64_t, D3D12_HEAP_TYPE, D3D12_RESOURCE_STATES);
void ResetChainWritten(ChainExecution&);
void RecordChain(ChainExecution&);
void ResetChainCommands(ChainExecution&);
void BenchmarkChain(ChainExecution&, const std::filesystem::path&);
ChainPacket LoadChainPacket(const std::filesystem::path& directory);
bool ReplayChain(const ChainPacket&, const std::filesystem::path& output, bool batched);
}
