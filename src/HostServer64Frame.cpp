#include "nrfusion/HostServer64.hpp"

namespace nrfusion {
namespace {

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

IpcFrameAckMessage HostServer64::ProcessFrame(const IpcFrameMessage& frameMsg) {
    IpcFrameStatus frameStatus = IpcFrameStatus::InvalidResources;
    bool submittedGpuWork = false;
    bool usedZeroGuides = false;
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
            bool commandListOpen = false;
            bool commandListReady = false;
            if (FAILED(allocator->Reset())) {
                frameStatus = IpcFrameStatus::InvalidResources;
            } else if (FAILED(d3d12CmdList_->Reset(allocator.Get(), nullptr))) {
                frameStatus = IpcFrameStatus::InvalidResources;
            } else {
                commandListOpen = true;
                if (FAILED(d3d12Queue_->Wait(
                        importedProducerFence_.Get(), frameMsg.producerFenceValue))) {
                    frameStatus = IpcFrameStatus::InvalidResources;
                } else {
                    commandListReady = true;
                }
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
                            if (shouldEvaluate) {
                                dlssNrAttemptedFrames_++;
                                usedZeroGuides = !importedDepth_ || !importedMotion_;
                            }

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

            if (submittedGpuWork && commandListOpen) {
                const HRESULT closeHr = d3d12CmdList_->Close();
                commandListOpen = false;
                if (SUCCEEDED(closeHr)) {
                    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
                    d3d12Queue_->ExecuteCommandLists(1, lists);

                    // Consumer completion must precede the host retirement marker on this queue.
                    if (SUCCEEDED(d3d12Queue_->Signal(
                            importedConsumerFence_.Get(),
                            frameMsg.consumerFenceValue))) {
                        const uint64_t hostFence = fenceValue_ + 1;
                        if (SUCCEEDED(d3d12Queue_->Signal(
                                d3d12Fence_.Get(), hostFence))) {
                            fenceValue_ = hostFence;
                            importedTransportFenceValue_ = hostFence;
                            allocFenceValues_[slot] = hostFence;
                            if (usedZeroGuides) guideUseFenceValue_ = hostFence;
                            frameStatus = IpcFrameStatus::Complete;
                        }
                    }
                }
            }
            if (commandListOpen) {
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
    frameAck.connectionGeneration = currentBuild_.connectionGeneration;
    frameAck.workId = frameMsg.workId;
    frameAck.completedFenceValue = frameStatus == IpcFrameStatus::Complete
        ? frameMsg.consumerFenceValue : 0;
    frameAck.status = static_cast<uint32_t>(frameStatus);

    return frameAck;
}

} // namespace nrfusion
