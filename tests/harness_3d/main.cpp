#include "D3D12TestHarness.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    nrfusion::testing::HarnessConfig config;
    config.frameCount = 120;
    config.headless = true;
    config.telemetryJsonPath = "harness_3d_telemetry.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            config.frameCount = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--export-telemetry" && i + 1 < argc) {
            config.telemetryJsonPath = argv[++i];
        } else if (arg == "--benchmark") {
            config.executionMode = nrfusion::testing::HarnessExecutionMode::Benchmark;
        } else if (arg == "--correctness") {
            config.executionMode = nrfusion::testing::HarnessExecutionMode::Correctness;
        } else if (arg == "--benchmark-iterations" && i + 1 < argc) {
            config.benchmarkIterations = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--test-async") {
            config.testAsync = true;
        } else if (arg == "--no-async") {
            config.testAsync = false;
        }
    }

    std::cout << "=================================================" << std::endl;
    std::cout << " NRFusion v0.5.4 - Autonomous 3D D3D12 Test Harness" << std::endl;
    std::cout << "=================================================" << std::endl;

    nrfusion::testing::D3D12TestHarness harness(config);
    if (!harness.Initialize()) {
        std::cerr << "ERROR: Failed to initialize D3D12 Test Harness." << std::endl;
        return 1;
    }

    if (!harness.Run()) {
        std::cerr << "ERROR: D3D12 Test Harness run failed." << std::endl;
        return 2;
    }

    if (config.executionMode == nrfusion::testing::HarnessExecutionMode::Benchmark) {
        std::cout << "SUCCESS: Harness benchmark completed." << std::endl;
    } else {
        std::cout << "SUCCESS: All 3D frames rendered and verified with NRFusion Auto." << std::endl;
    }
    return 0;
}
