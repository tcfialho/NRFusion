#include "nrfusion/HostServer64.hpp"

namespace nrfusion {

bool HostServer64::InitializeD3D12() {
    if (d3d12Device_) return true;

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            nullptr, D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC cqDesc{};
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(
            &cqDesc, IID_PPV_ARGS(&queue)))) {
        return false;
    }

    ComPtr<ID3D12CommandAllocator> alloc;
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&alloc)))) {
        return false;
    }

    std::array<ComPtr<ID3D12CommandAllocator>, kIpcMaxInFlight> allocs{};
    for (auto& slotAlloc : allocs) {
        if (FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&slotAlloc)))) {
            return false;
        }
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocs[0].Get(),
            nullptr, IID_PPV_ARGS(&commandList))) ||
        FAILED(commandList->Close())) {
        return false;
    }

    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return false;
    }

    auto synthetic = std::make_unique<SyntheticDx12Provider>();
    ProviderContext ctx{};
    ctx.api = GraphicsApi::D3D12;
    ctx.device = device.Get();
    ctx.commandQueue = queue.Get();
    ctx.preferSameDevice = true;
    synthetic->Initialize(ctx);

    auto dlssNr = std::make_unique<HostDlssNr>();
    if (dlssNr->Load()) dlssNr->Init(device.Get());

    d3d12Device_ = std::move(device);
    d3d12Queue_ = std::move(queue);
    d3d12Alloc_ = std::move(alloc);
    d3d12Allocs_ = std::move(allocs);
    d3d12CmdList_ = std::move(commandList);
    d3d12Fence_ = std::move(fence);
    syntheticProvider_ = std::move(synthetic);
    dlssNr_ = std::move(dlssNr);
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

bool HostServer64::WaitForGpuIdleAfterStop() noexcept {
    if (!d3d12Queue_ && !d3d12Fence_) return true;
    if (!d3d12Queue_ || !d3d12Fence_) return false;

    const uint64_t idleValue = fenceValue_ + 1;
    if (idleValue == 0 ||
        FAILED(d3d12Queue_->Signal(d3d12Fence_.Get(), idleValue))) {
        return false;
    }
    fenceValue_ = idleValue;
    if (d3d12Fence_->GetCompletedValue() >= idleValue) return true;

    HANDLE idleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!idleEvent) return false;
    const bool armed = SUCCEEDED(
        d3d12Fence_->SetEventOnCompletion(idleValue, idleEvent));
    const DWORD waitResult = armed
        ? WaitForSingleObject(idleEvent, 2000)
        : WAIT_FAILED;
    CloseHandle(idleEvent);
    return waitResult == WAIT_OBJECT_0 &&
        d3d12Fence_->GetCompletedValue() >= idleValue;
}

void HostServer64::Stop() {
    running_ = false;

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipeHandle_, nullptr);
        DisconnectNamedPipe(pipeHandle_);
    }
    if (eventHandle_) SetEvent(eventHandle_);
    if (workerThread_.joinable()) workerThread_.join();

    const bool gpuIdle = WaitForGpuIdleAfterStop();

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }
    clientConnected_ = false;
    if (!gpuIdle) return;

    RetireImportedTransport();
    CollectRetiredTransport();

    if (dlssNr_) {
        dlssNr_->Shutdown();
        dlssNr_.reset();
    }
    if (syntheticProvider_) {
        syntheticProvider_->Shutdown();
        syntheticProvider_.reset();
    }

    zeroGuideUpload_.Reset();
    lowGuideDepth_.Reset();
    lowGuideMotion_.Reset();
    guideCmdList_.Reset();
    guideAlloc_.Reset();
    guideFence_.Reset();
    guideWidth_ = guideHeight_ = 0;
    guideFenceValue_ = guideUseFenceValue_ = 0;

    d3d12CmdList_.Reset();
    for (auto& alloc : d3d12Allocs_) alloc.Reset();
    d3d12Alloc_.Reset();
    d3d12Fence_.Reset();
    d3d12Queue_.Reset();
    d3d12Device_.Reset();
    allocFenceValues_.fill(0);
    currentAllocSlot_ = 0;
    fenceValue_ = 0;
    importedTransportFenceValue_ = 0;
    currentBuild_ = {};
}

} // namespace nrfusion
