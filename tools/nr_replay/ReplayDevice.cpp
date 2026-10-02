#include "Replay.hpp"
#include "NrKernelArchitecture.hpp"
#include <stdexcept>

namespace nrreplay {
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}

GpuContext::~GpuContext() {
    if (completion) CloseHandle(completion);
}

void GpuContext::Initialize() {
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2 failed");
    ComPtr<IDXGIAdapter1> selected;
    for (unsigned index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        Check(adapter->GetDesc1(&desc), "Adapter descriptor failed");
        if (desc.VendorId == 0x10de && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            selected = adapter;
            break;
        }
    }
    if (!selected) throw std::runtime_error("Physical NVIDIA adapter not available");
    Check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice failed");
    if (!nrfusion::kernelprofile::SupportsSm89Device(device.Get()))
        throw std::runtime_error("Replay device architecture is not qualified (requires sm_89)");
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "Queue creation failed");
    Check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)), "Allocator creation failed");
    Check(device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "Command list creation failed");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Fence creation failed");
    completion = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completion) throw std::runtime_error("Fence event creation failed");
}

void GpuContext::SubmitAndWait() {
    Check(commands->Close(), "Command list close failed");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    Check(queue->Signal(fence.Get(), ++value), "Queue signal failed");
    if (fence->GetCompletedValue() < value) {
        Check(fence->SetEventOnCompletion(value, completion), "Fence registration failed");
        if (WaitForSingleObject(completion, 10000) != WAIT_OBJECT_0) throw std::runtime_error("GPU replay timeout");
    }
    Check(device->GetDeviceRemovedReason(), "GPU device removed during replay");
}
}
