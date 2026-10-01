#pragma once
#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Fence;
struct ID3D12GraphicsCommandList;

namespace nrfusion {
enum class NrGpuStage : unsigned { ModelBegin = 2, CompositionBegin = 3 };
using RecordNrGpuStage = void(*)(ID3D12GraphicsCommandList*, NrGpuStage) noexcept;
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
struct NrDiagnosticStages {
    std::uint64_t frame = 0;
    std::uint64_t valid = 0;
    double preparationGpuMs = 0;
    double modelGpuMs = 0;
    double compositionGpuMs = 0;
};
using ReadNrDiagnosticStages = int(*)(std::uint64_t, std::uint32_t, NrDiagnosticStages*);
} // namespace nrfusion
