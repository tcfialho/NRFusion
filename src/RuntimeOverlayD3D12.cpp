#include "RuntimeOverlayD3D12.hpp"
#include "RuntimeOverlayCursor.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/Logger.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <algorithm>

namespace nrfusion {

RuntimeOverlayD3D12& RuntimeOverlayD3D12::Instance() {
    static auto* renderer = new RuntimeOverlayD3D12;
    return *renderer;
}

bool RuntimeOverlayD3D12::Attach(IUnknown* candidate, IDXGISwapChain* swapChain) {
    ComPtr<ID3D12CommandQueue> queue;
    if (!candidate || !swapChain ||
        FAILED(candidate->QueryInterface(IID_PPV_ARGS(&queue))) ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
    std::lock_guard lock(renderMutex_);
    if (swapChain_ == swapChain && queue_.Get() == queue.Get()) return true;
    if (!Drain()) return false;
    ReleaseResources();
    queue_ = queue;
    swapChain_ = swapChain;
    NRF_LOG_INFO("OverlayD3D12", "Attached game swapchain=%p queue=%p", swapChain, queue.Get());
    return true;
}

bool RuntimeOverlayD3D12::Initialize(IDXGISwapChain3* swapChain) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow || !desc.BufferCount ||
        FAILED(queue_->GetDevice(IID_PPV_ARGS(&device_)))) return false;
    frames_.resize(desc.BufferCount);
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = desc.BufferCount;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_)))) return false;
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap_))) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) return false;
    rtvStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < desc.BufferCount; ++index) {
        auto& frame = frames_[index];
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&frame.allocator))) ||
            FAILED(swapChain->GetBuffer(index, IID_PPV_ARGS(&frame.backBuffer)))) return false;
        device_->CreateRenderTargetView(frame.backBuffer.Get(), nullptr, rtv);
        rtv.ptr += rtvStride_;
    }
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands_))) ||
        FAILED(commands_->Close())) return false;

    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    platformReady_ = ImGui_ImplWin32_Init(desc.OutputWindow);
    if (!platformReady_) return false;
    ImGui_ImplDX12_InitInfo init{};
    init.Device = device_.Get();
    init.CommandQueue = queue_.Get();
    init.NumFramesInFlight = static_cast<int>(desc.BufferCount);
    init.RTVFormat = desc.BufferDesc.Format;
    init.SrvDescriptorHeap = srvHeap_.Get();
    init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* settings,
        D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        *cpu = settings->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        *gpu = settings->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    };
    init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
        D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
    rendererReady_ = ImGui_ImplDX12_Init(&init);
    if (!rendererReady_ || !InitializeOverlayCursorControl() || !InstallInput(desc.OutputWindow)) return false;
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW display{};
    display.dmSize = sizeof(display);
    if (GetMonitorInfoW(MonitorFromWindow(desc.OutputWindow, MONITOR_DEFAULTTONEAREST), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &display))
        DlssgTransfusion::Instance().ObserveDisplayRefresh(display.dmDisplayFrequency);
    InitializeMetrics();
    RuntimeOverlay::Instance().SetInFrameRendering(true);
    NRF_LOG_INFO("OverlayD3D12", "ImGui ready hwnd=%p buffers=%u format=%u; no popup window",
                 desc.OutputWindow, desc.BufferCount, static_cast<UINT>(desc.BufferDesc.Format));
    return true;
}

void RuntimeOverlayD3D12::Draw(IDXGISwapChain* swapChain, UINT flags) {
    std::lock_guard lock(renderMutex_);
    LARGE_INTEGER cpuStart{};
    if (profiling_) QueryPerformanceCounter(&cpuStart);
    if (swapChain_ != swapChain || !queue_ || (flags & DXGI_PRESENT_TEST) || submissionFailed_) return;
    ComPtr<IDXGISwapChain3> swapChain3;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) return;
    if (!rendererReady_ && !Initialize(swapChain3.Get())) {
        NRF_LOG_ERROR("OverlayD3D12", "ImGui initialization failed");
        ReleaseResources();
        submissionFailed_ = true;
        return;
    }
    auto& overlay = RuntimeOverlay::Instance();
    if (!overlay.IsMenuOpen()) {
        if (wasOpen_) {
            ImGui::SetCurrentContext(context_);
            ImGui::GetIO().ClearInputKeys();
            std::lock_guard inputLock(inputMutex_);
            inputMessages_.clear();
        }
        wasOpen_ = false;
        RecordCpuMetrics(cpuStart, false);
        return;
    }
    wasOpen_ = true;
    const UINT slot = static_cast<UINT>(submission_ % frames_.size());
    auto& frame = frames_[slot];
    if (frame.fenceValue && fence_->GetCompletedValue() < frame.fenceValue) return;
    if (frame.fenceValue) CollectGpuMetrics(slot);
    const UINT backBuffer = swapChain3->GetCurrentBackBufferIndex();
    if (backBuffer >= frames_.size()) return;
    ImGui::SetCurrentContext(context_);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    FeedInput();
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui::NewFrame();
    DrawRuntimeOverlayImGui();
    ImGui::Render();
    if (FAILED(frame.allocator->Reset()) || FAILED(commands_->Reset(frame.allocator.Get(), nullptr))) {
        submissionFailed_ = true;
        NRF_LOG_ERROR("OverlayD3D12", "Command allocator/list reset failed");
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frames_[backBuffer].backBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commands_->ResourceBarrier(1, &barrier);
    auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(backBuffer) * rtvStride_;
    commands_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commands_->SetDescriptorHeaps(1, heaps);
    BeginGpuMetrics(slot);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands_.Get());
    EndGpuMetrics(slot);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands_->ResourceBarrier(1, &barrier);
    if (FAILED(commands_->Close())) {
        submissionFailed_ = true;
        NRF_LOG_ERROR("OverlayD3D12", "Command list close failed");
        return;
    }
    ID3D12CommandList* lists[] = {commands_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    frame.fenceValue = ++submission_;
    if (FAILED(queue_->Signal(fence_.Get(), submission_))) {
        submissionFailed_ = true;
        NRF_LOG_ERROR("OverlayD3D12", "Queue fence signal failed");
        return;
    }
    if ((drawCount_++ % 300) == 0)
        NRF_LOG_INFO("OverlayD3D12", "Menu submitted draw=%llu hwnd=%p foreground=%p iconic=%d",
                     drawCount_, window_, GetForegroundWindow(), IsIconic(window_));
    RecordCpuMetrics(cpuStart, true);
}

bool RuntimeOverlayD3D12::Drain() {
    if (!queue_ || !fence_ || !submission_) return true;
    if (device_ && FAILED(device_->GetDeviceRemovedReason())) return true;
    if (fence_->GetCompletedValue() >= submission_ && !submissionFailed_) return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const UINT64 completed = submission_ + 1;
    const bool signaled = SUCCEEDED(queue_->Signal(fence_.Get(), completed)) &&
                         SUCCEEDED(fence_->SetEventOnCompletion(completed, event));
    const bool drained = signaled && WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
    CloseHandle(event);
    if (!drained) NRF_LOG_ERROR("OverlayD3D12", "GPU drain failed; resources retained");
    return drained;
}

void RuntimeOverlayD3D12::ReleaseResources() {
    RemoveInput();
    if (context_) {
        ImGui::SetCurrentContext(context_);
        if (rendererReady_) ImGui_ImplDX12_Shutdown();
        if (platformReady_) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(context_);
    }
    context_ = nullptr;
    rendererReady_ = platformReady_ = submissionFailed_ = false;
    frames_.clear();
    commands_.Reset();
    fence_.Reset();
    srvHeap_.Reset();
    rtvHeap_.Reset();
    device_.Reset();
    submission_ = 0;
    timestampHeap_.Reset();
    timestampReadback_.Reset();
    profiling_ = wasOpen_ = false;
}

bool RuntimeOverlayD3D12::BeforeResize(IDXGISwapChain* swapChain) {
    std::lock_guard lock(renderMutex_);
    if (swapChain_ != swapChain) return true;
    if (!Drain()) return false;
    ReleaseResources();
    return true;
}

bool RuntimeOverlayD3D12::Reset() {
    std::lock_guard lock(renderMutex_);
    if (!Drain()) return false;
    ReleaseResources();
    queue_.Reset();
    swapChain_ = nullptr;
    return true;
}

} // namespace nrfusion
