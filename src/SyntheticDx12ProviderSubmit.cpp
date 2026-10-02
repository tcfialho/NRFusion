#include "nrfusion/SyntheticDx12Provider.hpp"

namespace nrfusion {

SyntheticWorkHandle SyntheticDx12Provider::Submit(const SyntheticFrameInputs& inputs, void* cmdListPtr) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid()) return handle;

    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);
    if (!cmdList) return handle;

    Resolution workRes = SyntheticDlaaContract::CalculateWorkingResolution(inputs.renderResolution, inputs.workingScale);
    if (!workRes.Valid()) return handle;

    uint32_t slotIdx = currentSlot_;
    currentSlot_ = (currentSlot_ + 1) % kRingSlots;

    if (!EnsureSlotResources(slotIdx, workRes)) {
        return handle;
    }

    Slot& slot = ringSlots_[slotIdx];
    slot.activeWorkId = inputs.ticket.id;
    slot.inUse = true;

    // If workingScale < 1.0, run GPU pre-downsample. If workingScale == 1.0,
    // initialize lowColor explicitly as well: the neural executor consumes lowColor
    // in both modes, so "native resolution" is not permission to leave it unwritten.
    if (inputs.workingScale < 0.999f) {
        auto* origColorRes = reinterpret_cast<ID3D12Resource*>(inputs.color.opaqueId);
        if (!origColorRes || !srvUavHeap_) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }
        if (origColorRes && srvUavHeap_) {
            Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            // 16 descriptors reserved per ring slot: Submit uses +0..2, ComposeNative +4..6,
            // ExtractResidual +8..10 -- kept apart so no two of a frame's own dispatches ever
            // overwrite a descriptor another one of that same frame's dispatches still needs.
            uint32_t baseDesc = slotIdx * 16;
            auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
            auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

            D3D12_CPU_DESCRIPTOR_HANDLE downSrvCpu = cpuHandle;
            downSrvCpu.ptr += (baseDesc + 0) * descriptorSize_;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(origColorRes, &srvDesc, downSrvCpu);

            D3D12_CPU_DESCRIPTOR_HANDLE downSrv1Cpu = cpuHandle;
            downSrv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
            device_->CreateShaderResourceView(origColorRes, &srvDesc, downSrv1Cpu);

            D3D12_CPU_DESCRIPTOR_HANDLE downUavCpu = cpuHandle;
            downUavCpu.ptr += (baseDesc + 2) * descriptorSize_;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(slot.lowColor.Get(), nullptr, &uavDesc, downUavCpu);

            ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
            cmdList->SetDescriptorHeaps(1, heaps);
            cmdList->SetComputeRootSignature(rootSignature_.Get());
            cmdList->SetPipelineState(downsamplePso_.Get());

            struct {
                uint32_t srcW, srcH, dstW, dstH;
                float pad[4];
            } downCb{ inputs.renderResolution.width, inputs.renderResolution.height, workRes.width, workRes.height, {0} };
            cmdList->SetComputeRoot32BitConstants(0, 8, &downCb, 0);

            D3D12_GPU_DESCRIPTOR_HANDLE downSrvGpu = gpuHandle;
            downSrvGpu.ptr += (baseDesc + 0) * descriptorSize_;
            cmdList->SetComputeRootDescriptorTable(1, downSrvGpu);

            D3D12_GPU_DESCRIPTOR_HANDLE downUavGpu = gpuHandle;
            downUavGpu.ptr += (baseDesc + 2) * descriptorSize_;
            cmdList->SetComputeRootDescriptorTable(2, downUavGpu);

            cmdList->Dispatch((workRes.width + 15) / 16, (workRes.height + 15) / 16, 1);

            Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }
    } else {
        auto* origColorRes = reinterpret_cast<ID3D12Resource*>(inputs.color.opaqueId);
        if (!origColorRes) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }

        const D3D12_RESOURCE_DESC sourceDesc = origColorRes->GetDesc();
        const D3D12_RESOURCE_DESC targetDesc = slot.lowColor->GetDesc();
        if (sourceDesc.Width != targetDesc.Width || sourceDesc.Height != targetDesc.Height ||
            sourceDesc.Format != targetDesc.Format || sourceDesc.SampleDesc.Count != targetDesc.SampleDesc.Count) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }

        Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyResource(slot.lowColor.Get(), origColorRes);
        Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }

    handle.workId = inputs.ticket.id;
    handle.valid = true;
    handle.workingScale = inputs.workingScale;
    handle.workResolution = workRes;
    handle.nativeResolution = inputs.renderResolution;

    uint64_t fVal = nextFenceValue_++;
    slot.fenceValue = fVal;
    handle.fenceValue = fVal;

    return handle;
}

bool SyntheticDx12Provider::Poll(const SyntheticWorkHandle& handle) {
    if (!ready_ || !handle.valid || !fence_) return false;
    return fence_->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticDx12Provider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    for (const auto& s : ringSlots_) {
        if (s.activeWorkId == handle.workId && s.lowNeuralOut) {
            ref.opaqueId = reinterpret_cast<uint64_t>(s.lowNeuralOut.Get());
            ref.resolution = s.resolution;
            ref.format = ResourceFormat::Rgba16Float;
            return ref;
        }
    }
    return ref;
}

uint32_t SyntheticDx12Provider::SlotForWork(uint64_t workId) const {
    for (uint32_t i = 0; i < ringSlots_.size(); ++i) {
        if (ringSlots_[i].activeWorkId == workId) return i;
    }
    return kRingSlots;
}


} // namespace nrfusion
