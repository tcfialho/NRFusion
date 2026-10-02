#pragma once
#include <cstdint>

namespace nrfusion::kernelprofile {
struct PointerObservation {
    std::uint64_t raw = 0, allocationBase = 0, allocationSize = 0, allocationId = 0;
    std::uint32_t memoryType = 0, deviceOrdinal = 0, mapped = 0;
    int queryResult = -1;
    bool matchesCudaAllocation = false;
};
void InitializePointerObservation();
PointerObservation ObservePointerBits(std::uint64_t candidate) noexcept;
}
