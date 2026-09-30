#pragma once
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include "nrfusion/RuntimeOverlay.hpp"

inline void BenchmarkRuntimeConfiguration(const char* filename) {
    auto& overlay = nrfusion::RuntimeOverlay::Instance();
    nrfusion::RuntimeConfig config{};
    nrfusion::RuntimeAdvancedConfig advanced{};
    std::array<double, 2000> microseconds{};
    unsigned checksum = 0;
    std::uint64_t generation = 0;
    unsigned snapshots = 0;
    for (unsigned index = 0; index < 1000; ++index)
        snapshots += overlay.GetActiveConfigurationIfChanged(generation, config, advanced);
    for (double& sample : microseconds) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned index = 0; index < 128; ++index) {
            snapshots += overlay.GetActiveConfigurationIfChanged(generation, config, advanced);
            checksum += static_cast<unsigned>(config.targetFps) + config.enabled;
        }
        sample = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count() / 128;
    }
    std::ofstream output(filename);
    output << "cpu_config_us\n";
    for (double sample : microseconds) output << sample << '\n';
    assert(output.good() && checksum != 0);
    assert(snapshots == 1);
}
