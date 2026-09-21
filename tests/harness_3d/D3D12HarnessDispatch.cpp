#include "D3D12TestHarness.hpp"

namespace nrfusion::testing {

bool D3D12TestHarness::Run() {
    if (config_.executionMode == HarnessExecutionMode::Benchmark) return RunBenchmark();
    return RunCorrectness();
}

} // namespace nrfusion::testing
