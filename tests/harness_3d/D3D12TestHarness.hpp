#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/CompatibilityDatabase.hpp"
#include "nrfusion/D3D12AsyncFenceBridge.hpp"
#include "nrfusion/D3D12QueueClockBridge.hpp"
#include "nrfusion/FrameContractProvider.hpp"
#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/NvofWrapper.hpp"
#include "nrfusion/ProfileStore.hpp"
#include "nrfusion/Sha256.hpp"
#include "nrfusion/Types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nrfusion::testing {

using Microsoft::WRL::ComPtr;

struct HarnessConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t frameCount = 120;
    bool headless = true;
    bool testAsync = true;
    std::string telemetryJsonPath = "";
};

struct FrameMetrics {
    std::uint64_t frameId = 0;
    double renderGpuMs = 0.0;
    double nrSimGpuMs = 0.0;
    double frameGpuMs = 0.0;
    double asyncOverlap = 0.0;
    bool asyncStable = false;
    float resolvedScale = 1.0f;
    nrfusion::SchedulerMode scheduler = nrfusion::SchedulerMode::Serialized;
    nrfusion::NrPrecision precision = nrfusion::NrPrecision::Fp8;
    bool autoDecisionSupported = false;
};

class D3D12TestHarness : public nrfusion::IFrameContractProvider {
public:
    explicit D3D12TestHarness(HarnessConfig config);
    ~D3D12TestHarness() override;

    bool Initialize();
    bool Run();

    // IFrameContractProvider implementation
    bool IsSupported(const nrfusion::GameContext& game) const override;
    nrfusion::FrameContext AcquireFrame(const nrfusion::ProviderInput& input) override;
    nrfusion::ProviderDiagnostics Diagnostics() const override;

    const std::vector<FrameMetrics>& Metrics() const noexcept { return metrics_; }
    const nrfusion::FusionRuntime& Runtime() const noexcept { return runtime_; }

private:
    bool InitializeDevice();
    bool CreateQueues();
    bool CreateRenderTargets();
    bool CreatePipelinesAndGeometry();
    bool CreateTimestampQueries();

    void RenderScene(std::uint64_t frameIndex, float angleRad, Jitter jitter, bool cameraCut);
    void SimulateNrPass(std::uint64_t frameIndex, SchedulerMode scheduler);
    void ExportTelemetryJson(const std::string& path);

    HarnessConfig config_;
    HWND hwnd_ = nullptr;

    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D12Device> device_;

    ComPtr<ID3D12CommandQueue> directQueue_;
    ComPtr<ID3D12CommandQueue> computeQueue_;
    ComPtr<ID3D12CommandAllocator> directAlloc_;
    ComPtr<ID3D12CommandAllocator> computeAlloc_;
    ComPtr<ID3D12GraphicsCommandList> directCmdList_;
    ComPtr<ID3D12GraphicsCommandList> computeCmdList_;

    ComPtr<ID3D12Fence> directFence_;
    ComPtr<ID3D12Fence> computeFence_;
    std::uint64_t directFenceValue_ = 0;
    std::uint64_t computeFenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    // Render Targets for Ground-Truth DLSS/NR Contract
    ComPtr<ID3D12Resource> colorBuffer_;
    ComPtr<ID3D12Resource> depthBuffer_;
    ComPtr<ID3D12Resource> motionBuffer_;
    ComPtr<ID3D12Resource> exposureBuffer_;
    ComPtr<ID3D12Resource> reactiveBuffer_;
    ComPtr<ID3D12Resource> computeScratchBuffer_;

    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;

    ComPtr<ID3D12RootSignature> rootSig_;
    ComPtr<ID3D12PipelineState> pso_;
    ComPtr<ID3D12RootSignature> computeRootSig_;
    ComPtr<ID3D12PipelineState> computePso_;
    ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // Timestamp query heaps
    ComPtr<ID3D12QueryHeap> timestampHeapDirect_;
    ComPtr<ID3D12QueryHeap> timestampHeapCompute_;
    ComPtr<ID3D12Resource> timestampReadbackDirect_;
    ComPtr<ID3D12Resource> timestampReadbackCompute_;
    double directGpuFreq_ = 1e7;
    double computeGpuFreq_ = 1e7;
    double cpuQpcFreq_ = 1e7;

    // Orchestration runtime and metrics
    nrfusion::FusionRuntime runtime_;
    std::vector<FrameMetrics> metrics_;
    std::string adapterName_;
    bool isNvidiaGpu_ = false;

    // Previous frame transforms for motion vector derivation
    float prevViewProj_[16]{};
    bool hasPrevFrame_ = false;

    // NVOF, ProfileStore, and CompatDB components
    nrfusion::NvofWrapper nvofWrapper_;
    std::string exeSha256_;
    std::string profilePath_ = "harness_profile_store.json";
    std::string compatPath_ = "compat/games.json";
};

} // namespace nrfusion::testing
