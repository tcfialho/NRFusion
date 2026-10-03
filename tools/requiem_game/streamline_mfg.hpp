#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct IDXGIFactory4;

namespace requiem {

class StreamlineMfg {
public:
    StreamlineMfg();
    ~StreamlineMfg();

    bool Initialize(std::uint32_t multiplier, bool required);
    bool SetDevice(ID3D12Device* native, ID3D12Device** proxyDevice);
    bool UpgradeFactory(IDXGIFactory4* native, IDXGIFactory4** proxyFactory);
    bool Configure(std::uint32_t outputWidth, std::uint32_t outputHeight,
                   std::uint32_t renderWidth, std::uint32_t renderHeight,
                   std::uint32_t backBuffers);
    void SetWindow(void* hwnd);

    bool BeginFramePacing(std::uint32_t frameIndex);
    bool BeginFrame(std::uint32_t frameIndex,
                    ID3D12GraphicsCommandList* commands,
                    ID3D12Resource* depth,
                    ID3D12Resource* motion,
                    std::uint32_t renderWidth,
                    std::uint32_t renderHeight,
                    bool reset);
    bool TagHudless(ID3D12GraphicsCommandList* commands,
                    ID3D12Resource* color,
                    ID3D12Resource* ui,
                    std::uint32_t width,
                    std::uint32_t height);

    void SimulationEnd();
    void RenderSubmitStart();
    void RenderSubmitEnd();
    void PresentStart();
    void PresentEnd();
    void Shutdown();

    bool Requested() const noexcept;
    bool Ready() const noexcept;
    bool GatePassed() const noexcept;
    std::uint64_t AppPresents() const noexcept;
    std::uint64_t ActualPresents() const noexcept;
    std::uint32_t ReportedMaxGenerated() const noexcept;
    std::uint32_t DlssgStatus() const noexcept;
    const std::string& Status() const noexcept;
    std::uint32_t GetStatsWindowMessage() const noexcept;
    void SetPendingPclPing(bool pending) noexcept;

    struct ReflexMetricPercentiles {
        double p50 = 0.0;
        double p95 = 0.0;
        double p99 = 0.0;
    };

    struct ReflexLatencyStats {
        bool available = false;
        std::uint64_t samples = 0;
        ReflexMetricPercentiles inputToSimStart{};
        ReflexMetricPercentiles inputToRenderSubmitStart{};
        ReflexMetricPercentiles inputToPresentStart{};
        ReflexMetricPercentiles inputToPresentEnd{};
        ReflexMetricPercentiles inputToDriverEnd{};
        ReflexMetricPercentiles inputToOsQueueEnd{};
        ReflexMetricPercentiles inputToGpuRenderEnd{};
    };
    ReflexLatencyStats GetReflexLatencyStats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace requiem
