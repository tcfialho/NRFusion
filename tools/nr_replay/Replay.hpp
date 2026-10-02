#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <nvapi.h>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace nrreplay {
using Bytes = std::vector<std::uint8_t>;
using Microsoft::WRL::ComPtr;
struct Packet {
    std::string kernel, moduleHash;
    Bytes image;
    std::array<std::uint8_t, 72> parameters{};
    std::array<Bytes, 5> initial;
    Bytes expectedOutput, expectedDone;
    NVAPI_DIM3 grid{}, block{};
    unsigned shared = 0;
};
struct GpuContext {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    HANDLE completion = nullptr;
    std::uint64_t value = 0;
    ~GpuContext();
    void Initialize();
    void SubmitAndWait();
};
void Check(HRESULT result, const char* operation);
Packet LoadPacket(const std::filesystem::path& directory);
void Benchmark(GpuContext&, const NVAPI_CU_KERNEL_LAUNCH_PARAMS&,
    decltype(&NvAPI_D3D12_LaunchCuKernelChain), const std::filesystem::path&, const std::string&);
bool ReplayStock(const Packet& packet, const std::filesystem::path& output,
                 const std::filesystem::path& customImage = {});
}
