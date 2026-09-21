#include "D3D12TestHarness.hpp"
#include <directxmath.h>
#include <iostream>
#include <utility>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
namespace nrfusion::testing {
using namespace DirectX;
D3D12TestHarness::D3D12TestHarness(HarnessConfig config) : config_(std::move(config)) {
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(prevViewProj_), XMMatrixIdentity());
}
D3D12TestHarness::~D3D12TestHarness() {
    if (directQueue_ && directFence_ && directFenceValue_ > 0) {
        directQueue_->Signal(directFence_.Get(), ++directFenceValue_);
        if (directFence_->GetCompletedValue() < directFenceValue_) {
            directFence_->SetEventOnCompletion(directFenceValue_, fenceEvent_);
            WaitForSingleObject(fenceEvent_, 2000);
        }
    }
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}
bool D3D12TestHarness::Initialize() {
    if (!InitializeDevice()) return false;
    if (!CreateQueues()) return false;
    if (!CreateRenderTargets()) return false;
    if (!CreatePipelinesAndGeometry()) return false;
    if (!CreateTimestampQueries()) return false;
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const auto shaOpt = nrfusion::Sha256File(exePath);
    if (shaOpt.has_value()) {
        exeSha256_ = *shaOpt;
    } else {
        exeSha256_ = "0000000000000000000000000000000000000000000000000000000000000000";
    }
    std::cout << "[Harness 3D] Binary: " << exePath << std::endl;
    std::cout << "[Harness 3D] SHA-256: " << exeSha256_ << std::endl;
    std::cout << "[Harness 3D] NVOF Available: " << (nvofWrapper_.IsAvailable() ? "YES" : "NO")
              << " (Path: " << nvofWrapper_.DllPath() << ")" << std::endl;
    return true;
}
bool D3D12TestHarness::InitializeDevice() {
    UINT dxgiFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif
    if (FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory_)))) {
        std::cerr << "Failed to create DXGI Factory 2" << std::endl;
        return false;
    }
    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1 bestDesc{};
    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter_->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (desc.VendorId == 0x10DE) {
            bestAdapter = adapter_;
            bestDesc = desc;
            isNvidiaGpu_ = true;
            break;
        }
        if (!bestAdapter) {
            bestAdapter = adapter_;
            bestDesc = desc;
        }
    }
    if (!bestAdapter) {
        std::cerr << "No suitable hardware GPU adapter found" << std::endl;
        return false;
    }
    adapter_ = bestAdapter;
    char nameBuf[256];
    WideCharToMultiByte(CP_UTF8, 0, bestDesc.Description, -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
    adapterName_ = nameBuf;
    std::cout << "[Harness 3D] Selected GPU: " << adapterName_
              << (isNvidiaGpu_ ? " (NVIDIA)" : " (Non-NVIDIA)") << std::endl;
    if (FAILED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
        std::cerr << "Failed to create D3D12 Device" << std::endl;
        return false;
    }
    return true;
}
bool D3D12TestHarness::CreateQueues() {
    D3D12_COMMAND_QUEUE_DESC directDesc{};
    directDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    directDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(device_->CreateCommandQueue(&directDesc, IID_PPV_ARGS(&directQueue_)))) {
        std::cerr << "Failed to create direct command queue" << std::endl;
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC computeDesc{};
    computeDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(device_->CreateCommandQueue(&computeDesc, IID_PPV_ARGS(&computeQueue_)))) {
        std::cerr << "Failed to create compute command queue" << std::endl;
        return false;
    }
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&directAlloc_))))
        return false;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeAlloc_))))
        return false;
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, directAlloc_.Get(),
                                         nullptr, IID_PPV_ARGS(&directCmdList_))))
        return false;
    directCmdList_->Close();
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, computeAlloc_.Get(),
                                         nullptr, IID_PPV_ARGS(&computeCmdList_))))
        return false;
    computeCmdList_->Close();
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&directFence_))))
        return false;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&computeFence_))))
        return false;
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) return false;
    LARGE_INTEGER qpcFreq{};
    QueryPerformanceFrequency(&qpcFreq);
    cpuQpcFreq_ = static_cast<double>(qpcFreq.QuadPart);
    UINT64 dFreq = 0, cFreq = 0;
    if (SUCCEEDED(directQueue_->GetTimestampFrequency(&dFreq)) && dFreq > 0)
        directGpuFreq_ = static_cast<double>(dFreq);
    if (SUCCEEDED(computeQueue_->GetTimestampFrequency(&cFreq)) && cFreq > 0)
        computeGpuFreq_ = static_cast<double>(cFreq);
    return true;
}
bool D3D12TestHarness::CreateTimestampQueries() {
    D3D12_QUERY_HEAP_DESC qDesc{};
    qDesc.Count = 4;
    qDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    if (FAILED(device_->CreateQueryHeap(&qDesc, IID_PPV_ARGS(&timestampHeapDirect_)))) return false;
    if (FAILED(device_->CreateQueryHeap(&qDesc, IID_PPV_ARGS(&timestampHeapCompute_)))) return false;
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = sizeof(std::uint64_t) * 4;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&timestampReadbackDirect_))))
        return false;
    if (FAILED(device_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&timestampReadbackCompute_))))
        return false;
    return true;
}
}
