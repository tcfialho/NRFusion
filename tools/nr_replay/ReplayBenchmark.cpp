#include "Replay.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace nrreplay {
void Benchmark(GpuContext& context, const NVAPI_CU_KERNEL_LAUNCH_PARAMS& kernel,
    decltype(&NvAPI_D3D12_LaunchCuKernelChain) launch,
    const std::filesystem::path& output, const std::string& variant) {
    constexpr unsigned warmup = 50, samples = 300;
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = samples * 2;
    ComPtr<ID3D12QueryHeap> queries;
    Check(context.device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queries)), "Benchmark query creation failed");
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = samples * 2 * sizeof(UINT64);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Check(context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Benchmark readback creation failed");
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    for (unsigned index = 0; index < warmup + samples; ++index) {
        context.commands->ResourceBarrier(1, &ordering);
        if (index >= warmup)
            context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index - warmup) * 2);
        if (launch(context.commands.Get(), &kernel, 1) != NVAPI_OK)
            throw std::runtime_error("Benchmark kernel launch failed");
        if (index >= warmup)
            context.commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (index - warmup) * 2 + 1);
    }
    context.commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, samples * 2, readback.Get(), 0);
    context.SubmitAndWait();
    UINT64 frequency = 0;
    Check(context.queue->GetTimestampFrequency(&frequency), "Benchmark timestamp frequency failed");
    if (!frequency) throw std::runtime_error("Invalid GPU timestamp frequency");
    UINT64* ticks = nullptr;
    D3D12_RANGE read{0, static_cast<SIZE_T>(buffer.Width)}, written{0, 0};
    Check(readback->Map(0, &read, reinterpret_cast<void**>(&ticks)), "Benchmark map failed");
    std::vector<double> microseconds;
    for (unsigned index = 0; index < samples; ++index) {
        if (ticks[index * 2 + 1] <= ticks[index * 2]) throw std::runtime_error("Invalid GPU timing interval");
        microseconds.push_back(double(ticks[index * 2 + 1] - ticks[index * 2]) * 1e6 / frequency);
    }
    readback->Unmap(0, &written);
    std::sort(microseconds.begin(), microseconds.end());
    std::filesystem::create_directories(output);
    std::ofstream report(output / (variant + "-benchmark.json"));
    report << std::setprecision(12) << "{\"warmup\":" << warmup << ",\"samples\":" << samples
        << ",\"median_us\":" << (microseconds[149] + microseconds[150]) / 2
        << ",\"p95_us\":" << microseconds[284] << ",\"p99_us\":" << microseconds[296]
        << ",\"scope\":\"isolated kernel GPU time; UAV ordering outside timestamp interval\"}\n";
    if (!report) throw std::runtime_error("Benchmark report failed");
    Check(context.allocator->Reset(), "Benchmark allocator reset failed");
    Check(context.commands->Reset(context.allocator.Get(), nullptr), "Benchmark commands reset failed");
}
}
