#include "RuntimeOverlayD3D12.hpp"
#include "nrfusion/Logger.hpp"
#include <algorithm>

namespace nrfusion {

void RuntimeOverlayD3D12::InitializeMetrics() {
    wchar_t value[8]{};
    profiling_ = GetEnvironmentVariableW(L"NRFUSION_OVERLAY_PROFILE", value, 8) && value[0] == L'1';
    if (!profiling_) return;
    QueryPerformanceFrequency(&cpuFrequency_);
    if (FAILED(queue_->GetTimestampFrequency(&timestampFrequency_))) return;
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = static_cast<UINT>(frames_.size()) * 2;
    if (FAILED(device_->CreateQueryHeap(&query, IID_PPV_ARGS(&timestampHeap_)))) return;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = static_cast<UINT64>(query.Count) * sizeof(UINT64);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&timestampReadback_)))) {
        timestampHeap_.Reset();
    }
}

void RuntimeOverlayD3D12::BeginGpuMetrics(UINT slot) {
    if (timestampHeap_ && timestampReadback_)
        commands_->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
}

void RuntimeOverlayD3D12::EndGpuMetrics(UINT slot) {
    if (!timestampHeap_ || !timestampReadback_) return;
    commands_->EndQuery(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);
    commands_->ResolveQueryData(timestampHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                slot * 2, 2, timestampReadback_.Get(), slot * 2 * sizeof(UINT64));
}

void RuntimeOverlayD3D12::CollectGpuMetrics(UINT slot) {
    if (!timestampReadback_ || !timestampFrequency_) return;
    const SIZE_T offset = static_cast<SIZE_T>(slot) * 2 * sizeof(UINT64);
    const D3D12_RANGE range{offset, offset + 2 * sizeof(UINT64)};
    void* mapped = nullptr;
    if (FAILED(timestampReadback_->Map(0, &range, &mapped))) return;
    const auto* timestamps = reinterpret_cast<const UINT64*>(static_cast<const BYTE*>(mapped) + offset);
    if (timestamps[1] >= timestamps[0]) {
        const double duration = (timestamps[1] - timestamps[0]) * 1000.0 / timestampFrequency_;
        gpuMilliseconds_ += duration;
        maxGpuMilliseconds_ = std::max(maxGpuMilliseconds_, duration);
        ++gpuSamples_;
    }
    const D3D12_RANGE written{0, 0};
    timestampReadback_->Unmap(0, &written);
}

void RuntimeOverlayD3D12::RecordCpuMetrics(LARGE_INTEGER start, bool open) {
    if (!profiling_ || !start.QuadPart || !cpuFrequency_.QuadPart) return;
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&end);
    if (metricMenuOpen_ != open) {
        metricSamples_ = gpuSamples_ = 0;
        cpuMilliseconds_ = gpuMilliseconds_ = maxCpuMilliseconds_ = maxGpuMilliseconds_ = 0;
        metricMenuOpen_ = open;
    }
    const double duration = (end.QuadPart - start.QuadPart) * 1000.0 / cpuFrequency_.QuadPart;
    cpuMilliseconds_ += duration;
    maxCpuMilliseconds_ = std::max(maxCpuMilliseconds_, duration);
    if (++metricSamples_ < 300) return;
    NRF_LOG_INFO("OverlayProfile", "open=%d samples=%u cpu_avg_ms=%.6f cpu_max_ms=%.6f gpu_samples=%u gpu_avg_ms=%.6f gpu_max_ms=%.6f",
        open, metricSamples_, cpuMilliseconds_ / metricSamples_, maxCpuMilliseconds_, gpuSamples_,
        gpuSamples_ ? gpuMilliseconds_ / gpuSamples_ : 0, maxGpuMilliseconds_);
    metricSamples_ = gpuSamples_ = 0;
    cpuMilliseconds_ = gpuMilliseconds_ = maxCpuMilliseconds_ = maxGpuMilliseconds_ = 0;
}

} // namespace nrfusion
