#pragma once

#include <cstddef>
#include <cstdint>

namespace nrfusion {

struct D3D12NrScratchAccounting {
    std::size_t resourceCount = 0;
    std::uint64_t logicalBytes = 0;
    bool logicalBytesExact = true;
};

} // namespace nrfusion
