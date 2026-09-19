#include "nrfusion/HostServer64.hpp"

#include <fstream>
#include <iostream>

namespace nrfusion {
namespace {

template <typename Interface>
bool OpenAndReleaseTargetHandle(ID3D12Device* device, uint64_t rawHandle,
                                Microsoft::WRL::ComPtr<Interface>& destination) {
    if (rawHandle == 0) return true;

    HANDLE targetHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(rawHandle));
    const bool opened = device != nullptr &&
        SUCCEEDED(device->OpenSharedHandle(targetHandle, IID_PPV_ARGS(&destination)));
    CloseHandle(targetHandle);
    return opened;
}

bool CopyCompatible(ID3D12Resource* source, ID3D12Resource* destination) {
    if (!source || !destination) {
        return false;
    }
    const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
    const D3D12_RESOURCE_DESC destinationDesc = destination->GetDesc();
    return sourceDesc.Dimension == destinationDesc.Dimension && sourceDesc.Width == destinationDesc.Width &&
           sourceDesc.Height == destinationDesc.Height && sourceDesc.DepthOrArraySize == destinationDesc.DepthOrArraySize &&
           sourceDesc.MipLevels == destinationDesc.MipLevels && sourceDesc.Format == destinationDesc.Format &&
           sourceDesc.SampleDesc.Count == destinationDesc.SampleDesc.Count &&
           sourceDesc.SampleDesc.Quality == destinationDesc.SampleDesc.Quality;
}

void TransitionSharedResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                              D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (!commandList || !resource || before == after) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

} // namespace

HostServer64::HostServer64() {
    eventHandle_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    overlapped_.hEvent = eventHandle_;
}

HostServer64::~HostServer64() {
    Stop();
    if (eventHandle_) {
        CloseHandle(eventHandle_);
        eventHandle_ = nullptr;
    }
}

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

bool HostServer64::EnsureZeroGuides(uint32_t width, uint32_t height) {
    if (lowGuideDepth_ && lowGuideMotion_ && guideWidth_ == width && guideHeight_ == height) {
        return true;
    }
    if (width == 0 || height == 0 || !d3d12Device_) return false;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 uploadSize = 0;
    d3d12Device_->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    zeroGuideUpload_.Reset();
    if (FAILED(d3d12Device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&zeroGuideUpload_)))) {
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(zeroGuideUpload_->Map(0, nullptr, &mapped))) return false;
    ZeroMemory(mapped, static_cast<size_t>(uploadSize));
    zeroGuideUpload_->Unmap(0, nullptr);

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    lowGuideDepth_.Reset();
    lowGuideMotion_.Reset();
    if (FAILED(d3d12Device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&lowGuideDepth_)))) {
        return false;
    }
    if (FAILED(d3d12Device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&lowGuideMotion_)))) {
        return false;
    }

    if (!guideAlloc_) {
        if (FAILED(d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&guideAlloc_)))) return false;
    }
    if (!guideCmdList_) {
        if (FAILED(d3d12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, guideAlloc_.Get(),
                                                   nullptr, IID_PPV_ARGS(&guideCmdList_)))) return false;
        guideCmdList_->Close();
    }
    if (!guideFence_) {
        if (FAILED(d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&guideFence_)))) return false;
    }

    guideAlloc_->Reset();
    guideCmdList_->Reset(guideAlloc_.Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = zeroGuideUpload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    for (ID3D12Resource* dst : { lowGuideDepth_.Get(), lowGuideMotion_.Get() }) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dst;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        guideCmdList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = dst;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        guideCmdList_->ResourceBarrier(1, &barrier);
    }

    guideCmdList_->Close();
    ID3D12CommandList* lists[] = { guideCmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);

    const uint64_t fenceVal = ++guideFenceValue_;
    d3d12Queue_->Signal(guideFence_.Get(), fenceVal);
    HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (evt) {
        guideFence_->SetEventOnCompletion(fenceVal, evt);
        WaitForSingleObject(evt, INFINITE);
        CloseHandle(evt);
    }

    guideWidth_ = width;
    guideHeight_ = height;
    return true;
}

bool HostServer64::Start(uint32_t hostPid) {
    if (running_) return true;

    // Optional D3D12 initialization (proceeds even if software/headless test)
    InitializeD3D12();

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

void HostServer64::ServerLoop() {
    while (running_) {
        ResetEvent(eventHandle_);
        BOOL connResult = ConnectNamedPipe(pipeHandle_, &overlapped_);
        if (!connResult) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                while (running_) {
                    DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                    if (waitRes == WAIT_OBJECT_0) break;
                }
                if (!running_) break;
            } else if (err == ERROR_PIPE_CONNECTED) {
                // Client already connected before ConnectNamedPipe
            } else {
                break;
            }
        }

        clientConnected_ = true;

        auto readExact = [this](void* buffer, DWORD size) -> bool {
            DWORD bytesRead = 0;
            ResetEvent(eventHandle_);
            BOOL ok = ReadFile(pipeHandle_, buffer, size, &bytesRead, &overlapped_);
            if (!ok) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    while (running_) {
                        DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                        if (waitRes == WAIT_OBJECT_0) break;
                    }
                    if (!running_) return false;
                    if (!GetOverlappedResult(pipeHandle_, &overlapped_, &bytesRead, FALSE)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return (bytesRead == size);
        };

        auto writeExact = [this](const void* buffer, DWORD size) -> bool {
            DWORD bytesWritten = 0;
            ResetEvent(eventHandle_);
            BOOL ok = WriteFile(pipeHandle_, buffer, size, &bytesWritten, &overlapped_);
            if (!ok) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    while (running_) {
                        DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                        if (waitRes == WAIT_OBJECT_0) break;
                    }
                    if (!running_) return false;
                    if (!GetOverlappedResult(pipeHandle_, &overlapped_, &bytesWritten, FALSE)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return (bytesWritten == size);
        };

        // 1. Handshake: Hello
        IpcHelloMessage hello{};
        if (!readExact(&hello, sizeof(hello)) || hello.magic != NRFUSION_IPC_MAGIC ||
            hello.version != NRFUSION_IPC_VERSION) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        IpcHelloAckMessage helloAck{};
        helloAck.magic = NRFUSION_IPC_MAGIC;
        helloAck.version = NRFUSION_IPC_VERSION;
        helloAck.hostPid = GetCurrentProcessId();
        helloAck.status = 0;
        if (!writeExact(&helloAck, sizeof(helloAck))) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // 2. Build configuration
        IpcBuildMessage build{};
        if (!readExact(&build, sizeof(build)) || build.magic != NRFUSION_IPC_MAGIC ||
            build.version != NRFUSION_IPC_VERSION || build.sessionId == 0) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // These handles were duplicated into this process by CaptureProvider32. They are one-shot
        // transport handles: after OpenSharedHandle succeeds or fails, this host must close them.
        // Keeping the raw client values would only work in an in-process test, not across x86/x64.
        importedColor_.Reset();
        importedResidual_.Reset();
        importedDepth_.Reset();
        importedMotion_.Reset();
        importedProducerFence_.Reset();
        importedConsumerFence_.Reset();

        const bool carriesSharedTransport = build.colorSharedHandle != 0 || build.residualSharedHandle != 0 ||
            build.producerFenceHandle != 0 || build.consumerFenceHandle != 0 ||
            build.depthSharedHandle != 0 || build.motionSharedHandle != 0;
        const bool modeKnown = build.processingMode == static_cast<uint32_t>(IpcProcessingMode::DummyCopy) ||
            build.processingMode == static_cast<uint32_t>(IpcProcessingMode::Neural);

        bool handlesOpened = true;
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.colorSharedHandle, importedColor_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.residualSharedHandle, importedResidual_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.depthSharedHandle, importedDepth_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.motionSharedHandle, importedMotion_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.producerFenceHandle, importedProducerFence_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.consumerFenceHandle, importedConsumerFence_);

        const bool transportComplete = !carriesSharedTransport ||
            (importedColor_ && importedResidual_ && importedProducerFence_ && importedConsumerFence_);
        currentBuild_ = build;

        IpcBuildAckMessage buildAck{};
        buildAck.magic = NRFUSION_IPC_MAGIC;
        buildAck.version = NRFUSION_IPC_VERSION;
        buildAck.sessionId = build.sessionId;
        const bool dummyRequiresTransport = build.processingMode == static_cast<uint32_t>(IpcProcessingMode::DummyCopy);
        buildAck.status = handlesOpened && transportComplete && modeKnown &&
            (!dummyRequiresTransport || carriesSharedTransport) ? 0u : 1u;
        buildAck.workWidth = static_cast<uint32_t>(build.width * build.workingScale);
        buildAck.workHeight = static_cast<uint32_t>(build.height * build.workingScale);
        if (!writeExact(&buildAck, sizeof(buildAck))) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }
        if (buildAck.status != 0) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // 3. Pipelined Frame streaming loop
        while (running_ && clientConnected_) {
            IpcFrameMessage frameMsg{};
            if (!readExact(&frameMsg, sizeof(frameMsg))) {
                // Client disconnected or pipe broken
                break;
            }

            if (frameMsg.magic != NRFUSION_IPC_MAGIC || frameMsg.version != NRFUSION_IPC_VERSION ||
                frameMsg.sessionId != currentBuild_.sessionId) {
                break;
            }

            IpcFrameStatus frameStatus = IpcFrameStatus::InvalidResources;
            bool submittedGpuWork = false;
            const bool hasSharedTransport = importedColor_ && importedResidual_ &&
                importedProducerFence_ && importedConsumerFence_;

            if (!hasSharedTransport) {
                // Control-only IPC remains valid for existing no-resource tests. The dummy-copy
                // mode, however, is never allowed to claim a GPU round-trip without all four
                // shared objects.
                if (currentBuild_.processingMode == static_cast<uint32_t>(IpcProcessingMode::Neural) &&
                    currentBuild_.colorSharedHandle == 0 && currentBuild_.residualSharedHandle == 0 &&
                    currentBuild_.producerFenceHandle == 0 && currentBuild_.consumerFenceHandle == 0) {
                    frameStatus = IpcFrameStatus::Complete;
                }
            } else {
                const uint32_t slot = currentAllocSlot_;
                currentAllocSlot_ = (currentAllocSlot_ + 1) % kIpcMaxInFlight;

                // A full ring is backpressure, not permission to stall the host CPU. The client
                // sees a non-complete ack and keeps presenting its latest completed N-1 output.
                const bool allocatorBusy = allocFenceValues_[slot] > 0 && d3d12Fence_ &&
                    d3d12Fence_->GetCompletedValue() < allocFenceValues_[slot];
                if (allocatorBusy) {
                    frameStatus = IpcFrameStatus::DroppedBackpressure;
                } else {
                    auto& allocator = d3d12Allocs_[slot] ? d3d12Allocs_[slot] : d3d12Alloc_;
                    bool commandListReady = false;
                    if (FAILED(allocator->Reset()) || FAILED(d3d12CmdList_->Reset(allocator.Get(), nullptr)) ||
                        FAILED(d3d12Queue_->Wait(importedProducerFence_.Get(), frameMsg.producerFenceValue))) {
                        frameStatus = IpcFrameStatus::InvalidResources;
                    } else {
                        commandListReady = true;
                    }
                    if (commandListReady && currentBuild_.processingMode == static_cast<uint32_t>(IpcProcessingMode::DummyCopy)) {
                        if (CopyCompatible(importedColor_.Get(), importedResidual_.Get())) {
                            TransitionSharedResource(d3d12CmdList_.Get(), importedColor_.Get(),
                                                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                            TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                            d3d12CmdList_->CopyResource(importedResidual_.Get(), importedColor_.Get());
                            TransitionSharedResource(d3d12CmdList_.Get(), importedColor_.Get(),
                                                     D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                            TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                     D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                            submittedGpuWork = true;
                        }
                    } else if (commandListReady && syntheticProvider_ && syntheticProvider_->IsReady()) {
                        SyntheticFrameInputs inputs{};
                        inputs.ticket.id = frameMsg.workId;
                        inputs.ticket.session = currentBuild_.sessionId;
                        inputs.ticket.viewKey = frameMsg.viewId;
                        inputs.ticket.configurationGeneration = currentBuild_.sessionId;
                        inputs.frameId = frameMsg.workId;
                        inputs.color.opaqueId = reinterpret_cast<uint64_t>(importedColor_.Get());
                        inputs.color.resolution = { currentBuild_.width, currentBuild_.height };
                        inputs.color.format = ResourceFormat::Rgba16Float;
                        const bool clientPreDownsampled = (currentBuild_.workingScale < 0.999f) &&
                                                          (currentBuild_.width != currentBuild_.targetWidth);
                        inputs.renderResolution = { currentBuild_.width, currentBuild_.height };
                        inputs.targetResolution = { currentBuild_.targetWidth, currentBuild_.targetHeight };
                        inputs.workingScale = clientPreDownsampled ? 1.0f : currentBuild_.workingScale;
                        inputs.jitter = { frameMsg.jitterX, frameMsg.jitterY };
                        inputs.reset = frameMsg.reset != 0;

                        SyntheticWorkHandle handle = syntheticProvider_->Submit(inputs, d3d12CmdList_.Get());
                        bool evaluated = false;
                        if (handle.workId != 0) {
                            if (dlssNr_ && dlssNr_->IsInitialised()) {
                                const Resolution workRes = SyntheticDlaaContract::CalculateWorkingResolution(
                                    inputs.renderResolution, inputs.workingScale);
                                const uint32_t ringSlot = syntheticProvider_->SlotForWork(handle.workId);
                                ID3D12Resource* lowColor = syntheticProvider_->GetSlotLowColor(ringSlot);
                                ID3D12Resource* lowNeuralOut = syntheticProvider_->GetSlotLowNeuralOut(ringSlot);

                                if (workRes.Valid() && lowColor && lowNeuralOut &&
                                    EnsureZeroGuides(workRes.width, workRes.height)) {
                                    const bool featureReady = dlssNr_->EnsureFeature(d3d12CmdList_.Get(), workRes.width,
                                                                                     workRes.height);
                                    const bool shouldEvaluate = featureReady && !dlssNr_->JustBuilt();
                                    if (shouldEvaluate) dlssNrAttemptedFrames_++;

                                    ID3D12Resource* depthRes = importedDepth_ ? importedDepth_.Get() : lowGuideDepth_.Get();
                                    ID3D12Resource* motionRes = importedMotion_ ? importedMotion_.Get() : lowGuideMotion_.Get();
                                    const uint32_t guideW = importedDepth_ ? currentBuild_.width : workRes.width;
                                    const uint32_t guideH = importedDepth_ ? currentBuild_.height : workRes.height;
                                    const uint32_t motionW = importedMotion_ ? currentBuild_.width : workRes.width;
                                    const uint32_t motionH = importedMotion_ ? currentBuild_.height : workRes.height;

                                    if (shouldEvaluate &&
                                        dlssNr_->Evaluate(d3d12CmdList_.Get(), lowColor, depthRes, motionRes,
                                                          lowNeuralOut, workRes.width, workRes.height,
                                                          currentBuild_.depthInverted != 0, inputs.reset, {},
                                                          guideW, guideH, motionW, motionH)) {
                                        dlssNrEvaluatedFrames_++;
                                        syntheticProvider_->ExtractResidual(handle, d3d12CmdList_.Get());

                                        if (clientPreDownsampled) {
                                            ID3D12Resource* lowResidual = syntheticProvider_->GetSlotLowResidual(ringSlot);
                                            if (lowResidual && CopyCompatible(lowResidual, importedResidual_.Get())) {
                                                TransitionSharedResource(d3d12CmdList_.Get(), lowResidual,
                                                                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                                                TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                                                d3d12CmdList_->CopyResource(importedResidual_.Get(), lowResidual);
                                                TransitionSharedResource(d3d12CmdList_.Get(), lowResidual,
                                                                         D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                                                TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                                         D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                                                evaluated = true;
                                                submittedGpuWork = true;
                                            }
                                        } else {
                                            ResourceRef nativeColor{};
                                            nativeColor.opaqueId = reinterpret_cast<uint64_t>(importedColor_.Get());
                                            nativeColor.resolution = { currentBuild_.width, currentBuild_.height };
                                            nativeColor.format = ResourceFormat::Rgba16Float;
                                            ResourceRef nativeOut{};
                                            nativeOut.opaqueId = reinterpret_cast<uint64_t>(importedResidual_.Get());
                                            nativeOut.resolution = { currentBuild_.width, currentBuild_.height };
                                            nativeOut.format = ResourceFormat::Rgba16Float;
                                            if (syntheticProvider_->ComposeNative(handle, nativeColor, nativeOut,
                                                                                  d3d12CmdList_.Get(), 1.0f)) {
                                                evaluated = true;
                                                submittedGpuWork = true;
                                            }
                                        }
                                    }
                                }
                            }

                            if (!evaluated) {
                                ID3D12Resource* fallbackSrc = importedColor_.Get();
                                if (fallbackSrc && CopyCompatible(fallbackSrc, importedResidual_.Get())) {
                                    TransitionSharedResource(d3d12CmdList_.Get(), fallbackSrc,
                                                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                                    TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                             D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                                    d3d12CmdList_->CopyResource(importedResidual_.Get(), fallbackSrc);
                                    TransitionSharedResource(d3d12CmdList_.Get(), fallbackSrc,
                                                             D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                                    TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                             D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                                    submittedGpuWork = true;
                                }
                            }
                        }
                    } else if (commandListReady && CopyCompatible(importedColor_.Get(), importedResidual_.Get())) {
                        TransitionSharedResource(d3d12CmdList_.Get(), importedColor_.Get(),
                                                 D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                        TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                 D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                        d3d12CmdList_->CopyResource(importedResidual_.Get(), importedColor_.Get());
                        TransitionSharedResource(d3d12CmdList_.Get(), importedColor_.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                        TransitionSharedResource(d3d12CmdList_.Get(), importedResidual_.Get(),
                                                 D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                        submittedGpuWork = true;
                    }

                    if (submittedGpuWork && SUCCEEDED(d3d12CmdList_->Close())) {
                        ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
                        d3d12Queue_->ExecuteCommandLists(1, lists);
                        const uint64_t hostFence = ++fenceValue_;
                        if (SUCCEEDED(d3d12Queue_->Signal(d3d12Fence_.Get(), hostFence)) &&
                            SUCCEEDED(d3d12Queue_->Signal(importedConsumerFence_.Get(),
                                                          frameMsg.consumerFenceValue))) {
                            allocFenceValues_[slot] = hostFence;
                            frameStatus = IpcFrameStatus::Complete;
                        }
                    } else if (commandListReady) {
                        d3d12CmdList_->Close();
                    }
                }
            }

            lastWorkId_ = frameMsg.workId;
            processedFrames_++;

            // 4) Write frame acknowledgment back to client
            IpcFrameAckMessage frameAck{};
            frameAck.magic = NRFUSION_IPC_MAGIC;
            frameAck.version = NRFUSION_IPC_VERSION;
            frameAck.sessionId = currentBuild_.sessionId;
            frameAck.workId = frameMsg.workId;
            frameAck.completedFenceValue = frameStatus == IpcFrameStatus::Complete
                ? frameMsg.consumerFenceValue : 0;
            frameAck.status = static_cast<uint32_t>(frameStatus);

            if (!writeExact(&frameAck, sizeof(frameAck))) {
                break;
            }
        }

        DisconnectNamedPipe(pipeHandle_);
        clientConnected_ = false;
        importedColor_.Reset();
        importedResidual_.Reset();
        importedDepth_.Reset();
        importedMotion_.Reset();
        importedProducerFence_.Reset();
        importedConsumerFence_.Reset();
    }
}

} // namespace nrfusion
