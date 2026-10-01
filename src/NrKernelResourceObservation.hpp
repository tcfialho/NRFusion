#pragma once
#include <cstdint>
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;

namespace nrfusion::kernelprofile {
struct BufferObservation {
    std::uint64_t allocationId = 0, base = 0, bytes = 0, offset = 0;
    std::uintptr_t resourceId = 0;
    std::uint32_t initialState = 0;
    std::uint32_t currentState = 0;
    bool matched = false;
};
bool StartResourceObservation(ID3D12Device* device);
void ResetObservedResources(ID3D12Device* device);
BufferObservation ObserveD3D12Buffer(std::uint64_t candidate) noexcept;
bool ObserveCommandBarriers(ID3D12GraphicsCommandList* commands);
ID3D12Resource* RetainObservedBuffer(std::uint64_t address, std::uint64_t bytes,
                                    std::uint64_t& offset, std::uint32_t& state);
}
