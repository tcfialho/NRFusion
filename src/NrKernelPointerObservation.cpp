#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "NrKernelPointerObservation.hpp"

namespace nrfusion::kernelprofile {
namespace {
using PointerAttributes = int(__stdcall*)(unsigned, const int*, void**, unsigned long long);
PointerAttributes attributes = nullptr;
}

void InitializePointerObservation() {
    const auto driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!driver) return;
    using Init = int(__stdcall*)(unsigned);
    const auto init = reinterpret_cast<Init>(GetProcAddress(driver, "cuInit"));
    if (!init || init(0) != 0) return;
    attributes = reinterpret_cast<PointerAttributes>(GetProcAddress(driver, "cuPointerGetAttributes"));
}

PointerObservation ObservePointerBits(std::uint64_t candidate) noexcept {
    PointerObservation observation{};
    observation.raw = candidate;
    if (!attributes || !candidate) return observation;
    constexpr int memoryType = 2, devicePointer = 3, bufferId = 7;
    constexpr int deviceOrdinal = 9, rangeStart = 11, rangeSize = 12, mapped = 13;
    const int requested[]{memoryType, devicePointer, bufferId, deviceOrdinal, rangeStart, rangeSize, mapped};
    std::uint64_t deviceAddress = 0;
    void* output[]{&observation.memoryType, &deviceAddress, &observation.allocationId,
        &observation.deviceOrdinal, &observation.allocationBase, &observation.allocationSize,
        &observation.mapped};
    observation.queryResult = attributes(7, requested, output, candidate);
    observation.matchesCudaAllocation = observation.queryResult == 0 && observation.memoryType == 2 &&
        observation.mapped && observation.allocationId && observation.allocationSize &&
        candidate >= observation.allocationBase && candidate - observation.allocationBase < observation.allocationSize;
    return observation;
}

} // namespace nrfusion::kernelprofile
