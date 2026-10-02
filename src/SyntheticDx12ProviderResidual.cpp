#include "nrfusion/SyntheticDx12Provider.hpp"

namespace nrfusion {

bool SyntheticDx12Provider::ExtractResidual(const SyntheticWorkHandle& handle, void* cmdListPtr) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !handle.valid || !cmdListPtr || !srvUavHeap_) return false;
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    const uint32_t slotIdx = SlotForWork(handle.workId);
    if (slotIdx >= kRingSlots) return false;
    Slot& slot = ringSlots_[slotIdx];
    if (!slot.lowColor || !slot.lowNeuralOut || !slot.lowResidual) return false;

    Transition(cmdList, slot.lowNeuralOut.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, slot.lowResidual.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Same 16-descriptor-per-slot layout Submit/ComposeNative use; +8..10 is this dispatch's own
    // sub-range so it never overwrites a descriptor either of those still needs this frame.
    uint32_t baseDesc = slotIdx * 16 + 8;
    auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
    auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srv0Cpu = cpuHandle;
    srv0Cpu.ptr += (baseDesc + 0) * descriptorSize_;
    device_->CreateShaderResourceView(slot.lowNeuralOut.Get(), &srvDesc, srv0Cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE srv1Cpu = cpuHandle;
    srv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
    device_->CreateShaderResourceView(slot.lowColor.Get(), &srvDesc, srv1Cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
    uavCpu.ptr += (baseDesc + 2) * descriptorSize_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(slot.lowResidual.Get(), nullptr, &uavDesc, uavCpu);

    ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(extractResidualPso_.Get());

    struct {
        uint32_t workWidth, workHeight;
        float preExposure, padding;
        float pad2[4];
    } cb{ slot.resolution.width, slot.resolution.height, 1.0f, 0.0f, {0} };
    cmdList->SetComputeRoot32BitConstants(0, 8, &cb, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = gpuHandle;
    srvGpu.ptr += (baseDesc + 0) * descriptorSize_;
    cmdList->SetComputeRootDescriptorTable(1, srvGpu);

    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = gpuHandle;
    uavGpu.ptr += (baseDesc + 2) * descriptorSize_;
    cmdList->SetComputeRootDescriptorTable(2, uavGpu);

    cmdList->Dispatch((slot.resolution.width + 15) / 16, (slot.resolution.height + 15) / 16, 1);

    Transition(cmdList, slot.lowNeuralOut.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, slot.lowResidual.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    return true;
}

bool SyntheticDx12Provider::ComposeNative(const SyntheticWorkHandle& handle,
                                          const ResourceRef& originalNative,
                                          const ResourceRef& destinationNative,
                                          void* cmdListPtr,
                                          float residualWeight) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !handle.valid || !cmdListPtr) return false;
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    auto* origRes = reinterpret_cast<ID3D12Resource*>(originalNative.opaqueId);
    auto* dstRes = reinterpret_cast<ID3D12Resource*>(destinationNative.opaqueId);
    if (!origRes || !dstRes) return false;

    ID3D12Resource* residualRes = nullptr;
    uint32_t slotIdx = 0;
    for (size_t i = 0; i < ringSlots_.size(); ++i) {
        if (ringSlots_[i].activeWorkId == handle.workId) {
            residualRes = ringSlots_[i].lowResidual.Get();
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }
    if (!residualRes) return false;

    // Transition states
    Transition(cmdList, dstRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmdList, origRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, residualRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    struct {
        uint32_t nativeW;
        uint32_t nativeH;
        uint32_t resW;
        uint32_t resH;
        float weight;
        float exposure;
        float pad0;
        float pad1;
    } cb{
        originalNative.resolution.width,
        originalNative.resolution.height,
        handle.workResolution.width,
        handle.workResolution.height,
        residualWeight,
        1.0f,
        0.0f,
        0.0f
    };

    if (srvUavHeap_) {
        uint32_t baseDesc = slotIdx * 16 + 4;
        auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srv0Cpu = cpuHandle;
        srv0Cpu.ptr += (baseDesc + 0) * descriptorSize_;
        device_->CreateShaderResourceView(origRes, &srvDesc, srv0Cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE srv1Cpu = cpuHandle;
        srv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
        device_->CreateShaderResourceView(residualRes, &srvDesc, srv1Cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
        uavCpu.ptr += (baseDesc + 2) * descriptorSize_;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(dstRes, nullptr, &uavDesc, uavCpu);

        ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetComputeRootSignature(rootSignature_.Get());
        cmdList->SetPipelineState(composeResidualPso_.Get());
        cmdList->SetComputeRoot32BitConstants(0, 8, &cb, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = gpuHandle;
        srvGpu.ptr += (baseDesc + 0) * descriptorSize_;
        cmdList->SetComputeRootDescriptorTable(1, srvGpu);

        D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = gpuHandle;
        uavGpu.ptr += (baseDesc + 2) * descriptorSize_;
        cmdList->SetComputeRootDescriptorTable(2, uavGpu);

        uint32_t dispatchX = (originalNative.resolution.width + 15) / 16;
        uint32_t dispatchY = (originalNative.resolution.height + 15) / 16;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }

    Transition(cmdList, dstRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, origRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, residualRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    return true;
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowColor(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowColor.Get();
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowResidual(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowResidual.Get();
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowNeuralOut(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowNeuralOut.Get();
}

uint64_t SyntheticDx12Provider::CompletedFenceValue() const {
    if (!fence_) return 0;
    return fence_->GetCompletedValue();
}

} // namespace nrfusion
