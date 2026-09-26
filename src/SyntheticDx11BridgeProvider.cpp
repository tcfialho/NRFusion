#include "nrfusion/SyntheticDx11BridgeProvider.hpp"

#include <utility>

#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

namespace nrfusion {

bool SyntheticDx11ResourcesCopyCompatible(
    ID3D11Resource* source, ID3D11Resource* destination,
    ID3D11Device* expectedDevice) noexcept {
    if (!source || !destination || !expectedDevice) return false;
    ComPtr<ID3D11Device> sourceDevice;
    ComPtr<ID3D11Device> destinationDevice;
    source->GetDevice(&sourceDevice);
    destination->GetDevice(&destinationDevice);
    if (sourceDevice.Get() != expectedDevice ||
        destinationDevice.Get() != expectedDevice)
        return false;

    ComPtr<ID3D11Texture2D> sourceTexture;
    ComPtr<ID3D11Texture2D> destinationTexture;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&sourceTexture))) ||
        FAILED(destination->QueryInterface(IID_PPV_ARGS(&destinationTexture))))
        return false;
    D3D11_TEXTURE2D_DESC a{}, b{};
    sourceTexture->GetDesc(&a);
    destinationTexture->GetDesc(&b);
    return a.Width == b.Width && a.Height == b.Height &&
        a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize &&
        a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count &&
        a.SampleDesc.Quality == b.SampleDesc.Quality;
}

bool SyntheticDx11FenceBridge::CreateSharedFencePair(
    ID3D11Device5* device11, ID3D12Device* device12,
    ComPtr<ID3D11Fence>& fence11, ComPtr<ID3D12Fence>& fence12) {
    if (FAILED(device11->CreateFence(
            0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence11))))
        return false;
    HANDLE handle = nullptr;
    if (FAILED(fence11->CreateSharedHandle(
            nullptr, GENERIC_ALL, nullptr, &handle)))
        return false;
    const HRESULT opened =
        device12->OpenSharedHandle(handle, IID_PPV_ARGS(&fence12));
    CloseHandle(handle);
    return SUCCEEDED(opened);
}

bool SyntheticDx11FenceBridge::BindAfterIdle(
    ID3D11Device* device11, ID3D11DeviceContext* context11,
    ID3D12Device* device12, ID3D12CommandQueue* queue12) {
    if (!device11 || !context11 || !device12 || !queue12) return false;
    ResetAfterIdle();
    ComPtr<ID3D11Device> contextDevice;
    context11->GetDevice(&contextDevice);
    if (contextDevice.Get() != device11) return false;
    ComPtr<ID3D12Device> queueDevice;
    if (FAILED(queue12->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
        queueDevice.Get() != device12)
        return false;
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    if (FAILED(device11->QueryInterface(IID_PPV_ARGS(&device5))) ||
        FAILED(context11->QueryInterface(IID_PPV_ARGS(&context4))))
        return false;
    if (!CreateSharedFencePair(
            device5.Get(), device12, inputFence11_, inputFence12_) ||
        !CreateSharedFencePair(
            device5.Get(), device12, outputFence11_, outputFence12_)) {
        ResetAfterIdle();
        return false;
    }
    d3d11Context_ = std::move(context4);
    d3d12Queue_ = queue12;
    return true;
}

void SyntheticDx11FenceBridge::ResetAfterIdle() noexcept {
    outputFence12_.Reset(); outputFence11_.Reset();
    inputFence12_.Reset(); inputFence11_.Reset();
    d3d12Queue_.Reset(); d3d11Context_.Reset();
    nextInputValue_ = 1; nextOutputValue_ = 1;
}

bool SyntheticDx11FenceBridge::QueueInputHandoff() noexcept {
    if (!d3d11Context_ || !d3d12Queue_ || !inputFence11_ || !inputFence12_)
        return false;
    const std::uint64_t value = nextInputValue_++;
    return SUCCEEDED(d3d11Context_->Signal(inputFence11_.Get(), value)) &&
        SUCCEEDED(d3d12Queue_->Wait(inputFence12_.Get(), value));
}

bool SyntheticDx11FenceBridge::QueueOutputHandoff() noexcept {
    if (!d3d11Context_ || !d3d12Queue_ || !outputFence11_ || !outputFence12_)
        return false;
    const std::uint64_t value = nextOutputValue_++;
    return SUCCEEDED(d3d12Queue_->Signal(outputFence12_.Get(), value)) &&
        SUCCEEDED(d3d11Context_->Wait(outputFence11_.Get(), value));
}


SyntheticDx11BridgeProvider::SyntheticDx11BridgeProvider() = default;

SyntheticDx11BridgeProvider::~SyntheticDx11BridgeProvider() {
    Shutdown();
}

bool SyntheticDx11BridgeProvider::Initialize(const ProviderContext& context) {
    std::scoped_lock lock(mutex_);
    if (ready_) return true;
    if (context.api != GraphicsApi::D3D11 || context.is32Bit || !context.device)
        return false;

    d3d11Device_ = static_cast<ID3D11Device*>(context.device);
    d3d11Device_->GetImmediateContext(&d3d11Context_);
    if (!CreatePrivateD3D12()) return false;
    if (!sync_.BindAfterIdle(
            d3d11Device_.Get(), d3d11Context_.Get(),
            d3d12Device_.Get(), d3d12Queue_.Get()))
        return false;

    ProviderContext d12Ctx{};
    d12Ctx.api = GraphicsApi::D3D12;
    d12Ctx.device = d3d12Device_.Get();
    d12Ctx.commandQueue = d3d12Queue_.Get();
    d12Ctx.preferSameDevice = true;
    if (!syntheticD3D12_.Initialize(d12Ctx)) return false;

    // Optional until a real NVOF dispatch/completion backend is integrated.
    (void)nvof_.Initialize(
        d3d12Device_.Get(), d3d12Queue_.Get(), 1920, 1080);
    ready_ = true;
    return true;
}

void SyntheticDx11BridgeProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!ready_) return;
    nvof_.Shutdown();
    syntheticD3D12_.Shutdown();
    // The inner provider drains the shared queue before bridge resources are released.
    CloseSharedHandles();
    for (auto& slot : sharedSlots_) slot.alloc.Reset();
    sync_.ResetAfterIdle();
    d3d12Fence_.Reset();
    d3d12CmdList_.Reset();
    d3d12Queue_.Reset();
    d3d12Device_.Reset();
    d3d11Context_.Reset();
    d3d11Device_.Reset();
    currentRes_ = {};
    nextFenceValue_ = 1;
    ready_ = false;
}

SyntheticWorkHandle SyntheticDx11BridgeProvider::Submit(
    const SyntheticFrameInputs& inputs, void*) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid() || !d3d12Fence_ ||
        inputs.color.format != ResourceFormat::Rgba16Float)
        return handle;
    if (!CreateSharedResources(
            inputs.renderResolution.width, inputs.renderResolution.height))
        return handle;

    const auto lease = slotTracker_.Acquire(
        inputs.ticket.id, d3d12Fence_->GetCompletedValue());
    if (!lease) return handle;
    SharedSlot& slot = sharedSlots_[lease->slot];
    auto* gameColor = reinterpret_cast<ID3D11Resource*>(inputs.color.opaqueId);
    if (!CopyInputToSlot(lease->slot, gameColor) ||
        !sync_.QueueInputHandoff()) {
        slotTracker_.Release(*lease);
        return handle;
    }

    const HRESULT allocHr = slot.alloc->Reset();
    const HRESULT listHr = SUCCEEDED(allocHr)
        ? d3d12CmdList_->Reset(slot.alloc.Get(), nullptr) : allocHr;
    if (FAILED(listHr)) {
        slotTracker_.Release(*lease);
        return handle;
    }

    SyntheticFrameInputs d12Inputs = inputs;
    d12Inputs.color.opaqueId = reinterpret_cast<std::uint64_t>(slot.d3d12Color.Get());
    d12Inputs.color.resolution = inputs.renderResolution;
    d12Inputs.color.format = ResourceFormat::Rgba16Float;
    handle = syntheticD3D12_.Submit(d12Inputs, d3d12CmdList_.Get());
    if (!handle.valid || FAILED(d3d12CmdList_->Close())) {
        slotTracker_.Release(*lease);
        return {};
    }

    ID3D12CommandList* lists[] = {d3d12CmdList_.Get()};
    d3d12Queue_->ExecuteCommandLists(1, lists);
    const std::uint64_t fenceValue = nextFenceValue_++;
    if (FAILED(d3d12Queue_->Signal(d3d12Fence_.Get(), fenceValue))) {
        slotTracker_.Release(*lease);
        return {};
    }
    if (!slotTracker_.MarkSubmitted(*lease, fenceValue)) return {};
    handle.fenceValue = fenceValue;
    return handle;
}

bool SyntheticDx11BridgeProvider::Poll(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    return ready_ && handle.valid && d3d12Fence_ &&
        d3d12Fence_->GetCompletedValue() >= handle.fenceValue;
}

bool SyntheticDx11BridgeProvider::RecordD3D11OutputConsume(
    const SyntheticWorkHandle& handle, ID3D11DeviceContext* context,
    ID3D11Resource* gameDestination) {
    std::scoped_lock lock(mutex_);
    if (!context || !gameDestination || context != d3d11Context_.Get() ||
        !handle.valid)
        return false;
    const auto slot = slotTracker_.Find(handle.workId);
    if (!slot || !sharedSlots_[*slot].d3d11Residual ||
        !SyntheticDx11ResourcesCopyCompatible(
            sharedSlots_[*slot].d3d11Residual.Get(), gameDestination,
            d3d11Device_.Get()) ||
        !sync_.QueueOutputHandoff())
        return false;
    context->CopyResource(gameDestination, sharedSlots_[*slot].d3d11Residual.Get());
    return true;
}

ResourceRef SyntheticDx11BridgeProvider::GetResidual(
    const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;
    const auto slot = slotTracker_.Find(handle.workId);
    if (!slot || !sharedSlots_[*slot].d3d12Residual) return ref;
    ref.opaqueId = reinterpret_cast<std::uint64_t>(
        sharedSlots_[*slot].d3d12Residual.Get());
    ref.resolution = currentRes_;
    ref.format = ResourceFormat::Rgba16Float;
    return ref;
}

bool SyntheticDx11BridgeProvider::ComposeNative(
    const SyntheticWorkHandle& handle, const ResourceRef& originalNative,
    const ResourceRef& destinationNative, void* commandList,
    float residualWeight) {
    return syntheticD3D12_.ComposeNative(
        handle, originalNative, destinationNative, commandList, residualWeight);
}

} // namespace nrfusion
