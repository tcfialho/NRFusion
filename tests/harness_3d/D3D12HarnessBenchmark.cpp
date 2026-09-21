#include "D3D12TestHarness.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace nrfusion::testing {
namespace {

double PercentileNs(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

bool LoadPrevious(const std::string& path, BenchmarkSummary& summary) {
    if (path.empty()) return false;
    std::ifstream in(path);
    return static_cast<bool>(
        in >> summary.iterations >> summary.notReady >> summary.unsupported >>
        summary.p50Ns >> summary.p95Ns >> summary.p99Ns);
}

void SaveCurrent(const std::string& path, const BenchmarkSummary& summary) {
    if (path.empty()) return;
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << summary.iterations << ' ' << summary.notReady << ' ' << summary.unsupported << ' '
        << summary.p50Ns << ' ' << summary.p95Ns << ' ' << summary.p99Ns << '\n';
}

}

bool D3D12TestHarness::RunBenchmark() {
    BenchmarkSummary previous{};
    const bool hasPrevious = LoadPrevious(config_.benchmarkReportPath, previous);

    GameContext game{};
    game.api = GraphicsApi::D3D12;
    game.nativeDlss = true;

    RuntimeCapabilities caps{};
    caps.nativeProvider = true;
    caps.preSr = true;
    caps.nativeMotion = true;
    caps.fp8 = true;

    TelemetrySample sample{};
    sample.dtSeconds = 1.0 / 60.0;
    sample.nrGpuMs = 1.0;
    sample.frameGpuMs = 8.0;
    sample.sourceFps = 125.0;
    sample.processedFps = 125.0;
    sample.queuePressure = 0.15;

    constexpr std::uint32_t kWarmupIterations = 256;
    for (std::uint32_t i = 0; i < kWarmupIterations; ++i) {
        const ProviderInput input{i + 1, i + 1};
        auto frame = AcquireFrame(input);
        if (!frame.ReadyForCore() || !runtime_.ResolveAuto(game, frame, sample, caps).supported)
            return false;
    }

    const auto iterations = std::max<std::uint32_t>(1, config_.benchmarkIterations);
    std::vector<double> samples(iterations);
    benchmark_ = {};
    benchmark_.iterations = iterations;

    for (std::uint32_t i = 0; i < iterations; ++i) {
        const ProviderInput input{i + kWarmupIterations + 1, i + kWarmupIterations + 1};
        const auto start = std::chrono::steady_clock::now();
        auto frame = AcquireFrame(input);
        const bool ready = frame.ReadyForCore();
        const auto decision = runtime_.ResolveAuto(game, frame, sample, caps);
        const auto end = std::chrono::steady_clock::now();

        if (!ready) ++benchmark_.notReady;
        if (!decision.supported) ++benchmark_.unsupported;
        samples[i] = std::chrono::duration<double, std::nano>(end - start).count();
    }
    if (benchmark_.notReady != 0 || benchmark_.unsupported != 0) return false;

    std::sort(samples.begin(), samples.end());
    benchmark_.p50Ns = PercentileNs(samples, 0.50);
    benchmark_.p95Ns = PercentileNs(samples, 0.95);
    benchmark_.p99Ns = PercentileNs(samples, 0.99);

    std::cout << "[Harness 3D] CPU benchmark Acquire+Validate+ResolveAuto"
              << " iterations=" << benchmark_.iterations
              << " p50=" << benchmark_.p50Ns << "ns"
              << " p95=" << benchmark_.p95Ns << "ns"
              << " p99=" << benchmark_.p99Ns << "ns\n";
    if (hasPrevious) {
        std::cout << "[Harness 3D] before/after"
                  << " p50=" << previous.p50Ns << "->" << benchmark_.p50Ns
                  << " p95=" << previous.p95Ns << "->" << benchmark_.p95Ns
                  << " p99=" << previous.p99Ns << "->" << benchmark_.p99Ns << "ns\n";
    }

    SaveCurrent(config_.benchmarkReportPath, benchmark_);
    return true;
}

} // namespace nrfusion::testing
