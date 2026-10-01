#include "ReplayChain.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace nrreplay {
namespace {
double Percentile(const std::vector<double>& sorted, double fraction) {
    const double position = (sorted.size() - 1) * fraction;
    const auto lower = static_cast<unsigned>(position);
    const auto upper = std::min(lower + 1, static_cast<unsigned>(sorted.size() - 1));
    return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}
}

void BenchmarkChain(ChainExecution& execution, const std::filesystem::path& output) {
    constexpr unsigned warmup = 50, samples = 300;
    D3D12_QUERY_HEAP_DESC description{};
    description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    description.Count = samples * 2;
    ComPtr<ID3D12QueryHeap> queries;
    Check(execution.context.device->CreateQueryHeap(&description, IID_PPV_ARGS(&queries)), "Chain query allocation failed");
    const auto readback = ChainBuffer(execution.context, samples * 2 * sizeof(UINT64),
                                      D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    for (unsigned index = 0; index < warmup + samples; ++index) {
        ResetChainWritten(execution);
        if (index >= warmup)
            execution.context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index - warmup) * 2);
        RecordChain(execution);
        if (index >= warmup)
            execution.context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index - warmup) * 2 + 1);
    }
    execution.context.commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
        samples * 2, readback.Get(), 0);
    execution.context.SubmitAndWait();
    UINT64 frequency = 0;
    Check(execution.context.queue->GetTimestampFrequency(&frequency), "Chain timestamp frequency unavailable");
    if (!frequency) throw std::runtime_error("Invalid chain GPU timestamp frequency");
    UINT64* ticks = nullptr;
    D3D12_RANGE range{0, samples * 2 * sizeof(UINT64)}, ignored{0, 0};
    Check(readback->Map(0, &range, reinterpret_cast<void**>(&ticks)), "Chain timestamp mapping failed");
    std::vector<double> durations;
    for (unsigned index = 0; index < samples; ++index) {
        if (ticks[index * 2 + 1] <= ticks[index * 2]) throw std::runtime_error("Invalid chain timestamp interval");
        durations.push_back(double(ticks[index * 2 + 1] - ticks[index * 2]) * 1e6 / frequency);
    }
    readback->Unmap(0, &ignored);
    std::sort(durations.begin(), durations.end());
    std::filesystem::create_directories(output);
    const char* variant = execution.batched ? "batched" : "separate";
    std::ofstream report(output / (std::string(variant) + "-chain-benchmark.json"));
    report << std::setprecision(12) << "{\"warmup\":" << warmup << ",\"samples\":" << samples
        << ",\"variant\":" << std::quoted(variant) << ",\"median_us\":" << Percentile(durations, .5)
        << ",\"p95_us\":" << Percentile(durations, .95) << ",\"p99_us\":" << Percentile(durations, .99)
        << ",\"scope\":\"isolated stock projection/expand/contract GPU time; reset copies outside interval\"}\n";
    if (!report) throw std::runtime_error("Chain benchmark report failed");
    ResetChainCommands(execution);
}
}
