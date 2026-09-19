#include "nrfusion/SyntheticDx11BridgeProvider.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

// Only one compiler understands a library request written in the source. Elsewhere it is an
// unknown pragma, which a build with warnings as errors refuses outright.
#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace nrfusion {

SyntheticDx11BridgeProvider::SyntheticDx11BridgeProvider() = default;

SyntheticDx11BridgeProvider::~SyntheticDx11BridgeProvider() {
    Shutdown();
}

bool SyntheticDx11BridgeProvider::Initialize(const ProviderContext& context) {
    std::scoped_lock lock(mutex_);
    if (ready_) return true;
    if (!context.device) return false;

    d3d11Device_ = static_cast<ID3D11Device*>(context.device);
    d3d11Device_->GetImmediateContext(&d3d11Context_);

    if (!CreatePrivateD3D12()) {
        return false;
    }

    ProviderContext d12Ctx{};
    d12Ctx.api = GraphicsApi::D3D12;
    d12Ctx.device = d3d12Device_.Get();
    d12Ctx.commandQueue = d3d12Queue_.Get();
    d12Ctx.preferSameDevice = true;

    if (!syntheticD3D12_.Initialize(d12Ctx)) {
        return false;
    }

    nvof_.Initialize(d3d12Device_.Get(), d3d12Queue_.Get(), 1920, 1080);

    ready_ = true;
    return true;
}


bool SyntheticDx11BridgeProvider::CreatePrivateD3D12() {
    ComPtr<IDXGIDevice> dxgiDev;
    if (FAILED(d3d11Device_.As(&dxgiDev))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDev->GetAdapter(&adapter))) return false;

    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device_)))) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d3d12Device_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&d3d12Queue_)))) {
        return false;
    }

    for (auto& s : sharedSlots_) {
        if (FAILED(d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc)))) {
            return false;
        }
    }

    if (FAILED(d3d12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sharedSlots_[0].alloc.Get(), nullptr, IID_PPV_ARGS(&d3d12CmdList_)))) {
        return false;
    }
    d3d12CmdList_->Close();

    if (FAILED(d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence_)))) {
        return false;
    }

    return true;
}

void SyntheticDx11BridgeProvider::CloseSharedHandles() {
    for (auto& s : sharedSlots_) {
        if (s.colorSharedHandle) {
            CloseHandle(s.colorSharedHandle);
            s.colorSharedHandle = nullptr;
        }
        if (s.residualSharedHandle) {
            CloseHandle(s.residualSharedHandle);
            s.residualSharedHandle = nullptr;
        }
        s.d3d11Color.Reset();
        s.d3d11Depth.Reset();
        s.d3d11Motion.Reset();
        s.d3d11Residual.Reset();
        s.d3d12Color.Reset();
        s.d3d12Residual.Reset();
        s.inUse = false;
        s.workId = 0;
    }
}

bool SyntheticDx11BridgeProvider::CreateSharedResources(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return false;
    if (currentRes_.width == width && currentRes_.height == height) return true;

    CloseSharedHandles();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    for (auto& s : sharedSlots_) {
        // 1. D3D11 Color Shared
        if (FAILED(d3d11Device_->CreateTexture2D(&desc, nullptr, &s.d3d11Color))) return false;
        ComPtr<IDXGIResource1> res1Color;
        if (FAILED(s.d3d11Color.As(&res1Color))) return false;
        if (FAILED(res1Color->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &s.colorSharedHandle))) return false;

        // Open on private D3D12 device
        if (FAILED(d3d12Device_->OpenSharedHandle(s.colorSharedHandle, IID_PPV_ARGS(&s.d3d12Color)))) return false;

        // 2. D3D11 Residual Shared
        if (FAILED(d3d11Device_->CreateTexture2D(&desc, nullptr, &s.d3d11Residual))) return false;
        ComPtr<IDXGIResource1> res1Res;
        if (FAILED(s.d3d11Residual.As(&res1Res))) return false;
        if (FAILED(res1Res->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &s.residualSharedHandle))) return false;

        // Open on private D3D12 device
        if (FAILED(d3d12Device_->OpenSharedHandle(s.residualSharedHandle, IID_PPV_ARGS(&s.d3d12Residual)))) return false;
    }

    currentRes_ = { width, height };
    nvof_.Initialize(d3d12Device_.Get(), d3d12Queue_.Get(), width, height);
    return true;
}

void SyntheticDx11BridgeProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!ready_) return;

    CloseSharedHandles();
    for (auto& s : sharedSlots_) {
        s.alloc.Reset();
    }
    nvof_.Shutdown();
    syntheticD3D12_.Shutdown();

    d3d12Fence_.Reset();
    d3d12CmdList_.Reset();
    d3d12Alloc_.Reset();
    d3d12Queue_.Reset();
    d3d12Device_.Reset();
    d3d11Context_.Reset();
    d3d11Device_.Reset();
    ready_ = false;
}

bool SyntheticDx11BridgeProvider::RecordD3D11InputCopy(ID3D11DeviceContext* ctx,
                                                      ID3D11Resource* gameColor,
                                                      ID3D11Resource* /*gameDepth*/,
                                                      ID3D11Resource* /*gameMotion*/) {
    if (!ctx || !gameColor) return false;
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (!slot.d3d11Color) return false;

    // GPU-only copy without touching CPU
    ctx->CopyResource(slot.d3d11Color.Get(), gameColor);
    return true;
}

bool SyntheticDx11BridgeProvider::RecordD3D11OutputConsume(ID3D11DeviceContext* ctx,
                                                          ID3D11Resource* gameDestination) {
    if (!ctx || !gameDestination) return false;
    SharedSlot& slot = sharedSlots_[currentSlot_];
    if (!slot.d3d11Residual) return false;

    // GPU-only copy back to game presentation buffer
    ctx->CopyResource(gameDestination, slot.d3d11Residual.Get());
    return true;
}

SyntheticWorkHandle SyntheticDx11BridgeProvider::Submit(const SyntheticFrameInputs& inputs, void* /*cmdListPtr*/) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid()) return handle;

    if (!CreateSharedResources(inputs.renderResolution.width, inputs.renderResolution.height)) {
        return handle;
    }

    uint32_t slotIdx = currentSlot_;
    currentSlot_ = (currentSlot_ + 1) % kMaxInFlight;

    SharedSlot& slot = sharedSlots_[slotIdx];

    // Resetting a command allocator while the GPU still executes a command list recorded from it
    // is illegal in D3D12 and corrupts driver-internal state; a mere 100ms timeout that ignored
    // the wait result let this fire under load (e.g. cold shader/PSO compilation right after
    // launch), which is what produced the unmapped-read crashes deep in nvwgf2umx.dll. Block for
    // real completion instead, and skip this frame's submission if the GPU is still unusually far
    // behind rather than reuse a slot it may still be touching.
    if (slot.producerFenceValue > 0 && d3d12Fence_->GetCompletedValue() < slot.producerFenceValue) {
        HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!evt) return handle;
        d3d12Fence_->SetEventOnCompletion(slot.producerFenceValue, evt);
        const DWORD wait = WaitForSingleObject(evt, 5000);
        CloseHandle(evt);
        if (wait != WAIT_OBJECT_0) return handle;
    }

    slot.workId = inputs.ticket.id;
    slot.inUse = true;

    uint64_t fVal = nextFenceValue_++;
    slot.producerFenceValue = fVal;

    // Delegate D3D12 GPU processing to the inner synthetic provider using the shared D3D12 texture
    SyntheticFrameInputs d12Inputs = inputs;
    d12Inputs.color.opaqueId = reinterpret_cast<uint64_t>(slot.d3d12Color.Get());
    d12Inputs.color.resolution = inputs.renderResolution;
    d12Inputs.color.format = ResourceFormat::Rgba16Float;

    HRESULT allocHr, listHr;
    if (slot.alloc) {
        allocHr = slot.alloc->Reset();
        listHr = SUCCEEDED(allocHr) ? d3d12CmdList_->Reset(slot.alloc.Get(), nullptr) : allocHr;
    } else {
        allocHr = d3d12Alloc_->Reset();
        listHr = SUCCEEDED(allocHr) ? d3d12CmdList_->Reset(d3d12Alloc_.Get(), nullptr) : allocHr;
    }
    if (FAILED(listHr)) return handle;

    handle = syntheticD3D12_.Submit(d12Inputs, d3d12CmdList_.Get());

    d3d12CmdList_->Close();
    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);
    d3d12Queue_->Signal(d3d12Fence_.Get(), fVal);

    handle.fenceValue = fVal;
    return handle;
}

bool SyntheticDx11BridgeProvider::Poll(const SyntheticWorkHandle& handle) {
    if (!ready_ || !handle.valid || !d3d12Fence_) return false;
    return d3d12Fence_->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticDx11BridgeProvider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    for (const auto& s : sharedSlots_) {
        if (s.workId == handle.workId && s.d3d12Residual) {
            ref.opaqueId = reinterpret_cast<uint64_t>(s.d3d12Residual.Get());
            ref.resolution = currentRes_;
            ref.format = ResourceFormat::Rgba16Float;
            return ref;
        }
    }
    return ref;
}

bool SyntheticDx11BridgeProvider::ComposeNative(const SyntheticWorkHandle& handle,
                                               const ResourceRef& originalNative,
                                               const ResourceRef& destinationNative,
                                               void* commandList,
                                               float residualWeight) {
    // Composes on the private D3D12 side before returning to D3D11
    return syntheticD3D12_.ComposeNative(handle, originalNative, destinationNative, commandList, residualWeight);
}

} // namespace nrfusion
