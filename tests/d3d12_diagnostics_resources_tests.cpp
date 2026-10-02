#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "D3D12TestDevice.hpp"
#include "nrfusion/NrD3D12Diagnostics.hpp"
#include "nrfusion/NrDiagnosticsApi.hpp"

#include <cassert>
#include <cstdint>

using Microsoft::WRL::ComPtr;

extern "C" int NRFusion_BeginNrDiagnosticFrame(
    ID3D12Device* device, std::uint64_t frame, int profile, const char* csvPath);
extern "C" int NRFusion_ReadNrDiagnosticFrame(
    ID3D12CommandQueue* queue, ID3D12Fence* fence,
    std::uint64_t completion, nrfusion::NrDiagnosticFrame* output);

namespace {

void WaitForFence(ID3D12Fence* fence, std::uint64_t value) {
    if (fence->GetCompletedValue() >= value) return;
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(eventHandle != nullptr);
    assert(SUCCEEDED(fence->SetEventOnCompletion(value, eventHandle)));
    assert(WaitForSingleObject(eventHandle, 5000) == WAIT_OBJECT_0);
    CloseHandle(eventHandle);
}

} // namespace

int main() {
    using namespace nrfusion;

    ComPtr<ID3D12Device> device = testing::CreateD3D12TestDevice();
    assert(device);

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    auto accounting = NrD3D12DiagnosticAccountingSnapshot();
    assert(accounting.gpuObjectCount == 0);
    assert(accounting.readbackBytes == 0);
    assert(accounting.queryCapacity == 0);

    for (std::uint64_t frame = 1; frame <= 2; ++frame) {
        assert(NRFusion_BeginNrDiagnosticFrame(device.Get(), frame, 0, nullptr) == 1);
        accounting = NrD3D12DiagnosticAccountingSnapshot();
        assert(accounting.gpuObjectCount == 2);
        assert(accounting.readbackBytes == 32768ull * sizeof(std::uint64_t));
        assert(accounting.queryCapacity == 32768);

        assert(SUCCEEDED(queue->Signal(fence.Get(), frame)));
        WaitForFence(fence.Get(), frame);

        NrDiagnosticFrame output{};
        assert(NRFusion_ReadNrDiagnosticFrame(
            queue.Get(), fence.Get(), frame, &output) == 1);
        assert(output.frame == frame);

        accounting = NrD3D12DiagnosticAccountingSnapshot();
        assert(accounting.gpuObjectCount == 0);
        assert(accounting.readbackBytes == 0);
        assert(accounting.queryCapacity == 0);
    }

    return 0;
}
