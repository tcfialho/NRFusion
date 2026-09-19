#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Fence;

namespace nrfusion {
// Diagnostic-only ABI. The caller retires its command list before reading these results.
struct NrDiagnosticFrame {
    std::uint64_t frame = 0;
    std::uint64_t passes = 0;
    std::uint64_t successfulPasses = 0;
    std::uint64_t kernelLaunches = 0;
    std::uint64_t chainCalls = 0;
    double nrGpuMs = 0;
};
using BeginNrDiagnosticFrame = int(*)(ID3D12Device*, std::uint64_t, int, const char*);
using ReadNrDiagnosticFrame = int(*)(ID3D12CommandQueue*, ID3D12Fence*, std::uint64_t, NrDiagnosticFrame*);
} // namespace nrfusion
