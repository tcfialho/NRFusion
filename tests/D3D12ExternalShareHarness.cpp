#include "D3D12ExternalShareHarness.hpp"

namespace nrfusion::test {
namespace {

using Microsoft::WRL::ComPtr;

void CloseIfPresent(HANDLE& handle) noexcept {
    if (handle) CloseHandle(handle);
    handle = nullptr;
}

bool SelectAdapter(
    IDXGIFactory6* factory,
    bool hardwareRequired,
    ComPtr<IDXGIAdapter1>& adapter) {
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        if (factory->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        candidate->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            continue;
        if (SUCCEEDED(D3D12CreateDevice(
                candidate.Get(),
                D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device),
                nullptr))) {
            adapter = candidate;
            return true;
        }
    }

    if (hardwareRequired) return false;

    ComPtr<IDXGIAdapter> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
        return false;
    return SUCCEEDED(warp.As(&adapter));
}

} // namespace

D3D12ExternalShareHarness::~D3D12ExternalShareHarness() {
    Close();
}

bool D3D12ExternalShareHarness::Open(bool hardwareRequired) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
        !SelectAdapter(factory.Get(), hardwareRequired, adapter_) ||
        FAILED(D3D12CreateDevice(
            adapter_.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)))) {
        return false;
    }

    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter_->GetDesc1(&adapterDesc);
    adapterLuid_ = adapterDesc.AdapterLuid;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device_->CreateCommandQueue(
            &queueDesc, IID_PPV_ARGS(&queue_)))) {
        return false;
    }

    if (!CreateSharedTexture(color_, colorHandle_) ||
        !CreateSharedTexture(output_, outputHandle_)) {
        return false;
    }

    D3D12_RESOURCE_DESC desc = color_->GetDesc();
    const auto info =
        device_->GetResourceAllocationInfo(0, 1, &desc);
    allocationSize_ = info.SizeInBytes;
    if (allocationSize_ == 0) return false;

    if (FAILED(device_->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(&producerFence_))) ||
        FAILED(device_->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(&consumerFence_))) ||
        FAILED(device_->CreateSharedHandle(
            producerFence_.Get(), nullptr,
            GENERIC_ALL, nullptr,
            &producerFenceHandle_)) ||
        FAILED(device_->CreateSharedHandle(
            consumerFence_.Get(), nullptr,
            GENERIC_ALL, nullptr,
            &consumerFenceHandle_))) {
        return false;
    }

    return true;
}

bool D3D12ExternalShareHarness::CreateSharedTexture(
    ComPtr<ID3D12Resource>& resource,
    HANDLE& sharedHandle) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (FAILED(device_->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_SHARED,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&resource)))) {
        return false;
    }

    return SUCCEEDED(device_->CreateSharedHandle(
        resource.Get(), nullptr,
        GENERIC_ALL, nullptr, &sharedHandle));
}

bool D3D12ExternalShareHarness::SignalProducer(
    std::uint64_t value) noexcept {
    return queue_ && producerFence_ &&
        SUCCEEDED(queue_->Signal(producerFence_.Get(), value));
}

bool D3D12ExternalShareHarness::WaitConsumer(
    std::uint64_t value,
    std::uint32_t timeoutMs) noexcept {
    if (!consumerFence_) return false;
    if (consumerFence_->GetCompletedValue() >= value) return true;

    HANDLE eventHandle =
        CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!eventHandle) return false;

    const HRESULT eventHr =
        consumerFence_->SetEventOnCompletion(value, eventHandle);
    const DWORD waitResult = SUCCEEDED(eventHr)
        ? WaitForSingleObject(eventHandle, timeoutMs)
        : WAIT_FAILED;
    CloseHandle(eventHandle);
    return waitResult == WAIT_OBJECT_0;
}

void D3D12ExternalShareHarness::Close() noexcept {
    if (queue_) queue_->Signal(producerFence_.Get(), 0);

    CloseIfPresent(consumerFenceHandle_);
    CloseIfPresent(producerFenceHandle_);
    CloseIfPresent(outputHandle_);
    CloseIfPresent(colorHandle_);

    consumerFence_.Reset();
    producerFence_.Reset();
    output_.Reset();
    color_.Reset();
    queue_.Reset();
    device_.Reset();
    adapter_.Reset();

    allocationSize_ = 0;
    adapterLuid_ = {};
}

} // namespace nrfusion::test
