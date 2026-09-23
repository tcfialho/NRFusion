#include "nrfusion/HostServer64.hpp"

namespace nrfusion {

bool HostServer64::InitializeD3D12() {
    if (d3d12Device_) {
        return true;
    }

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device_));
    if (FAILED(hr)) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC cqDesc{};
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = d3d12Device_->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&d3d12Queue_));
    if (FAILED(hr)) {
        return false;
    }

    hr = d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&d3d12Alloc_));
    if (FAILED(hr)) {
        return false;
    }

    for (auto& alloc : d3d12Allocs_) {
        hr = d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
        if (FAILED(hr)) return false;
    }

    hr = d3d12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3d12Allocs_[0].Get(), nullptr, IID_PPV_ARGS(&d3d12CmdList_));
    if (FAILED(hr)) {
        return false;
    }
    d3d12CmdList_->Close();

    hr = d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d3d12Fence_));
    if (FAILED(hr)) {
        return false;
    }

    syntheticProvider_ = std::make_unique<SyntheticDx12Provider>();
    ProviderContext ctx{};
    ctx.api = GraphicsApi::D3D12;
    ctx.device = d3d12Device_.Get();
    ctx.commandQueue = d3d12Queue_.Get();
    ctx.preferSameDevice = true;
    syntheticProvider_->Initialize(ctx);

    // Non-fatal: nvngx.dll_dlssnr.dll / nvngx_dlssnr.dll missing beside this executable leaves the
    // host doing the downsample step only, same as before this pass -- never a hard failure to start.
    dlssNr_ = std::make_unique<HostDlssNr>();
    if (dlssNr_->Load()) {
        dlssNr_->Init(d3d12Device_.Get());
    }

    return true;
}

bool HostServer64::Start(uint32_t hostPid) {
    if (running_) return true;

    char pipeName[128];
    FormatPipeName(pipeName, sizeof(pipeName), hostPid);

    for (int attempt = 0; attempt != 50; ++attempt) {
        pipeHandle_ = CreateNamedPipeA(
            pipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            65536,
            65536,
            0,
            nullptr
        );
        if (pipeHandle_ != INVALID_HANDLE_VALUE) break;
        Sleep(20);
    }

    if (pipeHandle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    running_ = true;
    workerThread_ = std::thread(&HostServer64::ServerLoop, this);
    return true;
}

void HostServer64::Stop() {
    if (!running_) return;

    running_ = false;

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipeHandle_, nullptr);
        DisconnectNamedPipe(pipeHandle_);
    }

    if (eventHandle_) {
        SetEvent(eventHandle_);
    }

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    if (d3d12Fence_ && fenceValue_ > 0 &&
        d3d12Fence_->GetCompletedValue() < fenceValue_) {
        HANDLE idleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (idleEvent) {
            if (SUCCEEDED(d3d12Fence_->SetEventOnCompletion(
                    fenceValue_, idleEvent))) {
                WaitForSingleObject(idleEvent, 2000);
            }
            CloseHandle(idleEvent);
        }
    }

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }

    for (auto& alloc : d3d12Allocs_) {
        alloc.Reset();
    }

    clientConnected_ = false;
    importedColor_.Reset();
    importedResidual_.Reset();
    importedDepth_.Reset();
    importedMotion_.Reset();
    importedProducerFence_.Reset();
    importedConsumerFence_.Reset();
}

} // namespace nrfusion
