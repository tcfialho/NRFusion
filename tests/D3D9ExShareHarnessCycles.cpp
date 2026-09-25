#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {

bool D3D9ExShareHarness::RunRoundTrips(
    std::uint32_t count) {
    const std::uint64_t before = carrierCopies_;
    for (std::uint32_t cycle = 0;
         cycle != count; ++cycle) {
        if (!ProveNonBlockingHandoff())
            return false;
    }
    return carrierCopies_ - before ==
           static_cast<std::uint64_t>(count) * 4;
}

bool D3D9ExShareHarness::RunResetCycles(
    std::uint32_t count) {
    const std::uint64_t before = carrierCopies_;
    for (std::uint32_t cycle = 0;
         cycle != count; ++cycle) {
        if (!ProveResetPersistence() ||
            !ProveNonBlockingHandoff()) {
            return false;
        }
    }
    return carrierCopies_ - before ==
           static_cast<std::uint64_t>(count) * 4;
}

bool D3D9ExShareHarness::RunRecreationCycles(
    std::uint32_t count) {
    const std::uint64_t before = carrierCopies_;
    for (std::uint32_t cycle = 0;
         cycle != count; ++cycle) {
        const std::uint32_t width =
            32 + (cycle % 3) * 16;
        const std::uint32_t height =
            32 + (cycle % 2) * 16;
        if (!ProveSharedTexture(width, height) ||
            !ProveNonBlockingHandoff()) {
            return false;
        }
    }
    return carrierCopies_ - before ==
           static_cast<std::uint64_t>(count) * 4;
}

} // namespace nrfusion::test
