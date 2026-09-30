#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <deque>
#include <mutex>
#include <vector>

struct ImGuiContext;

namespace nrfusion {

class RuntimeOverlayD3D12 {
public:
    static RuntimeOverlayD3D12& Instance();
    bool Attach(IUnknown* queue, IDXGISwapChain* swapChain);
    void Draw(IDXGISwapChain* swapChain, UINT flags);
    bool BeforeResize(IDXGISwapChain* swapChain);
    bool Reset();

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct FrameResources {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> backBuffer;
        UINT64 fenceValue = 0;
    };
    bool Initialize(IDXGISwapChain3* swapChain);
    bool Drain();
    void ReleaseResources();
    bool InstallInput(HWND window);
    void RemoveInput();
    void FeedInput();
    void InitializeMetrics();
    void CollectGpuMetrics(UINT slot);
    void BeginGpuMetrics(UINT slot);
    void EndGpuMetrics(UINT slot);
    void RecordCpuMetrics(LARGE_INTEGER start, bool open);
    static LRESULT CALLBACK GameWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    std::mutex renderMutex_;
    std::mutex inputMutex_;
    std::deque<MSG> inputMessages_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12GraphicsCommandList> commands_;
    ComPtr<ID3D12Fence> fence_;
    std::vector<FrameResources> frames_;
    ImGuiContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    HWND window_ = nullptr;
    WNDPROC originalWndProc_ = nullptr;
    UINT rtvStride_ = 0;
    UINT64 submission_ = 0;
    UINT64 drawCount_ = 0;
    bool rendererReady_ = false;
    bool platformReady_ = false;
    bool submissionFailed_ = false;
    bool wasOpen_ = false;
    bool profiling_ = false;
    ComPtr<ID3D12QueryHeap> timestampHeap_;
    ComPtr<ID3D12Resource> timestampReadback_;
    UINT64 timestampFrequency_ = 0;
    LARGE_INTEGER cpuFrequency_{};
    UINT metricSamples_ = 0;
    UINT gpuSamples_ = 0;
    double cpuMilliseconds_ = 0;
    double gpuMilliseconds_ = 0;
    double maxCpuMilliseconds_ = 0;
    double maxGpuMilliseconds_ = 0;
    bool metricMenuOpen_ = false;
};

void DrawRuntimeOverlayImGui();

} // namespace nrfusion
