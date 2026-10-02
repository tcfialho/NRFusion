#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>

namespace nrfusion {

inline std::uint64_t QueryProcessDedicatedVram(ID3D12Device* device) {
    if (!device) {
        return 0;
    }

    const LUID luid = device->GetAdapterLuid();

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        return 0;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
        return 0;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(adapter.As(&adapter3))) {
        return 0;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo{};
    if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo))) {
        return 0;
    }

    return memoryInfo.CurrentUsage;
}

} // namespace nrfusion
