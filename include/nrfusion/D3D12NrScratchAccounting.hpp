#pragma once

#include <cstddef>
#include <cstdint>

namespace nrfusion {

struct D3D12NrScratchAccounting {
    std::size_t resourceCount = 0;
    std::size_t retiredCount = 0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t physicalBytes = 0;
    std::uint64_t peakLogicalBytes = 0;
    std::uint64_t peakPhysicalBytes = 0;
    bool logicalBytesExact = true;
    bool physicalBytesExact = true;
};

} // namespace nrfusion
