#include "D3D12TestHarness.hpp"

#include <iostream>

namespace nrfusion::testing {

bool D3D12TestHarness::ReportCorrectnessSummary(
    std::uint32_t asyncCount, std::uint32_t serialCount,
    bool preFallbackAsync, bool fallbackSerialized, bool recoveredAsync) const {
    std::cout << "[Harness 3D] Execution summary: "
              << asyncCount << " AsyncCompute frames, "
              << serialCount << " Serialized frames." << std::endl;

    if (config_.testAsync) {
        if (asyncCount == 0) {
            std::cerr << "ERROR: Expected AsyncCompute frames but none were selected." << std::endl;
            return false;
        }
        if (config_.frameCount >= 61 && !fallbackSerialized) {
            std::cerr << "ERROR: Expected Serialized fallback inside frames 61..90." << std::endl;
            return false;
        }
        if (config_.frameCount >= 91) {
            if (!preFallbackAsync) {
                std::cerr << "ERROR: AsyncCompute was not qualified before fallback." << std::endl;
                return false;
            }
            if (!recoveredAsync) {
                std::cerr << "ERROR: AsyncCompute did not recover after frame 90." << std::endl;
                return false;
            }
            std::cout << "[Harness 3D] Async -> fallback -> recovery cycle verified." << std::endl;
        }
    }

    std::cout << "[Harness 3D] Completed " << config_.frameCount << " frames successfully." << std::endl;
    return true;
}

} // namespace nrfusion::testing
