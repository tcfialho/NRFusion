#include "D3D12TestHarness.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

bool ParseScenario(std::string_view value, nrfusion::testing::HarnessScenario& scenario) {
    using nrfusion::testing::HarnessScenario;
    if (value == "all") scenario = HarnessScenario::All;
    else if (value == "steady") scenario = HarnessScenario::Steady;
    else if (value == "resize") scenario = HarnessScenario::Resize;
    else if (value == "reset") scenario = HarnessScenario::Reset;
    else if (value == "missing-guides") scenario = HarnessScenario::MissingGuides;
    else if (value == "provenance") scenario = HarnessScenario::Provenance;
    else if (value == "on-off") scenario = HarnessScenario::Toggle;
    else if (value == "failure") scenario = HarnessScenario::Failure;
    else return false;
    return true;
}

bool NextValue(int argc, char* argv[], int& index, std::string_view& value) {
    if (index + 1 >= argc) return false;
    value = argv[++index];
    return !value.empty() && !value.starts_with("--");
}

bool ParsePositiveU32(std::string_view value, std::uint32_t& output) {
    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed == 0) return false;
    output = parsed;
    return true;
}

int CliError(std::string_view message) {
    std::cerr << "ERROR: " << message << '\n';
    return 3;
}

}

int main(int argc, char* argv[]) {
    nrfusion::testing::HarnessConfig config;
    config.frameCount = 120;
    config.headless = true;
    config.telemetryJsonPath = "harness_3d_telemetry.json";

    bool benchmarkRequested = false;
    bool correctnessRequested = false;
    bool scenarioRequested = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        std::string_view value;

        if (arg == "--frames") {
            if (!NextValue(argc, argv, i, value) || !ParsePositiveU32(value, config.frameCount))
                return CliError("--frames requires a positive integer.");
        } else if (arg == "--export-telemetry") {
            if (!NextValue(argc, argv, i, value)) return CliError("--export-telemetry requires a path.");
            config.telemetryJsonPath = value;
        } else if (arg == "--benchmark") {
            benchmarkRequested = true;
            config.executionMode = nrfusion::testing::HarnessExecutionMode::Benchmark;
        } else if (arg == "--correctness") {
            correctnessRequested = true;
            config.executionMode = nrfusion::testing::HarnessExecutionMode::Correctness;
        } else if (arg == "--benchmark-iterations") {
            if (!NextValue(argc, argv, i, value) ||
                !ParsePositiveU32(value, config.benchmarkIterations))
                return CliError("--benchmark-iterations requires a positive integer.");
        } else if (arg == "--benchmark-report") {
            if (!NextValue(argc, argv, i, value)) return CliError("--benchmark-report requires a path.");
            config.benchmarkReportPath = value;
        } else if (arg == "--scenario") {
            if (!NextValue(argc, argv, i, value)) return CliError("--scenario requires a name.");
            if (!ParseScenario(value, config.scenario)) return CliError("Unknown harness scenario.");
            scenarioRequested = true;
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--width") {
            if (!NextValue(argc, argv, i, value) || !ParsePositiveU32(value, config.width))
                return CliError("--width requires a positive integer.");
        } else if (arg == "--height") {
            if (!NextValue(argc, argv, i, value) || !ParsePositiveU32(value, config.height))
                return CliError("--height requires a positive integer.");
        } else if (arg == "--test-async") {
            config.testAsync = true;
        } else if (arg == "--no-async") {
            config.testAsync = false;
        } else {
            return CliError(std::string("Unknown option: ") + std::string(arg));
        }
    }

    if (benchmarkRequested && correctnessRequested)
        return CliError("--benchmark and --correctness are mutually exclusive.");
    if (benchmarkRequested && scenarioRequested)
        return CliError("--benchmark cannot be combined with --scenario.");

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
    } else if (config.scenario != nrfusion::testing::HarnessScenario::Legacy) {
        std::cout << "SUCCESS: Harness scenario completed." << std::endl;
    } else {
        std::cout << "SUCCESS: All 3D frames rendered and verified with NRFusion Auto." << std::endl;
    }
    return 0;
}
