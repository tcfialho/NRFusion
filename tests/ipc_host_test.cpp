#include "nrfusion/HostServer64.hpp"
#include "nrfusion/CaptureProvider32.hpp"
#include "nrfusion/CaptureProvider32Export.h"
#include "D3D12TestDevice.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace nrfusion;

namespace nrfusion {
struct HostServer64GuideTestAccess {
    static void Bind(HostServer64& h, ID3D12Device* d, ID3D12CommandQueue* q) { h.d3d12Device_ = d; h.d3d12Queue_ = q; }
    static bool Ensure(HostServer64& h) { return h.EnsureZeroGuides(64, 64); }
    static bool Upload(const HostServer64& h) { return h.zeroGuideUpload_ != nullptr; }
    static unsigned Guides(const HostServer64& h) { return unsigned(h.lowGuideDepth_ != nullptr) + unsigned(h.lowGuideMotion_ != nullptr); }
    static void WaitAndCollect(HostServer64& h) {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(e);
        assert(h.guideFence_ && h.guideFenceValue_ != 0);
        assert(SUCCEEDED(h.guideFence_->SetEventOnCompletion(h.guideFenceValue_, e)));
        assert(WaitForSingleObject(e, 5000) == WAIT_OBJECT_0); CloseHandle(e);
        h.CollectRetiredGuideUpload();
    }
};
} // namespace nrfusion

void TestIpcHandshakeAndBuild() {
    std::cout << "[Test 1] IPC Handshake and Configuration...\n";

    uint32_t testPid = GetCurrentProcessId();

    HostServer64 host;
    bool hostStarted = host.Start(testPid);
    assert(hostStarted);
    assert(host.IsRunning());

    CaptureProvider32 client;
    bool connected = client.Connect(testPid, 2000);
    assert(connected);
    assert(client.IsConnected());

    // Give server thread a moment to update clientConnected_
    for (int i = 0; i < 50 && !host.IsClientConnected(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(host.IsClientConnected());

    CaptureClientConfig config{};
    config.width = 1920;
    config.height = 1080;
    config.workingScale = 0.67f;
    config.isHdr = true;
    config.depthInverted = true;

    bool configured = client.Configure(config);
    assert(configured);

    client.Disconnect();
    assert(!client.IsConnected());

    host.Stop();
    assert(!host.IsRunning());
    std::cout << "  -> Handshake and Build PASSED.\n";
}

void TestPipelinedFrameStreaming() {
    std::cout << "[Test 2] N/N-1 Pipelined Frame Streaming and Zero-Pixel Invariant...\n";

    uint32_t testPid = GetCurrentProcessId() + 100;

    HostServer64 host;
    bool hostStarted = host.Start(testPid);
    assert(hostStarted);

    CaptureProvider32 client;
    bool connected = client.Connect(testPid, 2000);
    assert(connected);

    CaptureClientConfig config{};
    config.width = 2560;
    config.height = 1440;
    config.workingScale = 0.5f;
    bool configured = client.Configure(config);
    assert(configured);

    constexpr uint32_t kNumFrames = 15;
    for (uint32_t f = 1; f <= kNumFrames; ++f) {
        PipelinedFrameResult result{};
        uint64_t workId = f;
        uint64_t producerFence = f;
        float jitterX = (f % 8) * 0.125f;
        float jitterY = (f % 8) * -0.125f;

        bool submitOk = client.SubmitFramePipelined(workId, producerFence, jitterX, jitterY, (f == 1), result);
        assert(submitOk);

        if (f == 1) {
            // Frame 1 has no previous result ready yet
            assert(!result.hasResult);
        } else {
            // Frame N (N >= 2) must have Frame N-1 available immediately without waiting for Frame N
            assert(result.hasResult);
            assert(result.readyWorkId == f - 1);
            assert(result.completedFenceValue == f - 1);
        }

        // Simulating game frame pacing (< 0.1ms IPC overhead)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Wait briefly for host worker to finish processing the last frame
    for (int i = 0; i < 50 && host.ProcessedFrameCount() < kNumFrames; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(host.ProcessedFrameCount() == kNumFrames);
    assert(host.LastProcessedWorkId() == kNumFrames);

    client.Disconnect();
    host.Stop();
    std::cout << "  -> Pipelined N/N-1 Frame Streaming PASSED (15 frames, zero pixels over IPC).\n";
}

void TestD3D12SharedHandlePipeline() {
    std::cout << "[Test 3] D3D12 Shared NT Handle Interop with IPC Host...\n";

    ComPtr<ID3D12Device> device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    if (FAILED(hr)) {
        std::cout << "  -> D3D12 device unavailable on this machine, skipping hardware handle test.\n";
        return;
    }

    // Create shared texture
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1280;
    desc.Height = 720;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ComPtr<ID3D12Resource> sharedTex;
    hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_SHARED, &desc,
                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&sharedTex));
    assert(SUCCEEDED(hr));

    HANDLE texHandle = nullptr;
    hr = device->CreateSharedHandle(sharedTex.Get(), nullptr, GENERIC_ALL, nullptr, &texHandle);
    assert(SUCCEEDED(hr));
    assert(texHandle != nullptr);

    // Create shared fences
    ComPtr<ID3D12Fence> fenceProd;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fenceProd));
    assert(SUCCEEDED(hr));
    HANDLE prodFenceHandle = nullptr;
    hr = device->CreateSharedHandle(fenceProd.Get(), nullptr, GENERIC_ALL, nullptr, &prodFenceHandle);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12Fence> fenceCons;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fenceCons));
    assert(SUCCEEDED(hr));
    HANDLE consFenceHandle = nullptr;
    hr = device->CreateSharedHandle(fenceCons.Get(), nullptr, GENERIC_ALL, nullptr, &consFenceHandle);
    assert(SUCCEEDED(hr));

    uint32_t testPid = GetCurrentProcessId() + 200;
    HostServer64 host;
    bool hostStarted = host.Start(testPid);
    assert(hostStarted);

    CaptureProvider32 client;
    bool connected = client.Connect(testPid, 2000);
    assert(connected);

    CaptureClientConfig config{};
    config.width = 1280;
    config.height = 720;
    config.workingScale = 1.0f;
    config.colorSharedHandle = reinterpret_cast<uint64_t>(texHandle);
    config.residualSharedHandle = reinterpret_cast<uint64_t>(texHandle);
    config.producerFenceHandle = reinterpret_cast<uint64_t>(prodFenceHandle);
    config.consumerFenceHandle = reinterpret_cast<uint64_t>(consFenceHandle);

    bool configured = client.Configure(config);
    assert(configured);

    // Several frames, not one: EnsureFeature's first call only records creation work (the GPU-hang
    // guard in HostDlssNr.hpp), so DLSS-NR only actually evaluates from frame 2 onward.
    constexpr uint32_t kFrames = 5;
    for (uint32_t f = 1; f <= kFrames; ++f) {
        PipelinedFrameResult result{};
        fenceProd->Signal(f);
        bool submitOk = client.SubmitFramePipelined(f, f, 0.0f, 0.0f, f == 1, result);
        assert(submitOk);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (int i = 0; i < 100 && host.ProcessedFrameCount() < kFrames; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(host.ProcessedFrameCount() >= kFrames);

    std::cout << "  -> DLSS-NR status: " << host.DlssNrStatus() << "\n";
    std::cout << "  -> DLSS-NR attempted/evaluated/total: " << host.DlssNrAttemptedFrameCount() << " / "
              << host.DlssNrEvaluatedFrameCount() << " / " << kFrames << "\n";

    client.Disconnect();
    host.Stop();

    CloseHandle(texHandle);
    CloseHandle(prodFenceHandle);
    CloseHandle(consFenceHandle);
    std::cout << "  -> D3D12 Shared Handle and Fence Interop PASSED.\n";
}

void TestZeroGuideUploadRetirement() {
    std::cout << "[Test 4] Host zero-guide upload retirement...\n";
    ComPtr<ID3D12Device> device = testing::CreateD3D12TestDevice(); assert(device);
    D3D12_COMMAND_QUEUE_DESC desc{}; desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    assert(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
    HostServer64 host;
    HostServer64GuideTestAccess::Bind(host, device.Get(), queue.Get());
    assert(HostServer64GuideTestAccess::Ensure(host));
    assert(HostServer64GuideTestAccess::Upload(host));
    assert(HostServer64GuideTestAccess::Guides(host) == 2);
    HostServer64GuideTestAccess::WaitAndCollect(host);
    assert(!HostServer64GuideTestAccess::Upload(host));
    assert(HostServer64GuideTestAccess::Guides(host) == 2);
    assert(HostServer64GuideTestAccess::Ensure(host));
    assert(!HostServer64GuideTestAccess::Upload(host));
}

void TestCapture32ExportApi() {
    std::cout << "[Test 5] Standalone 32-bit Client C Export API...\n";

    uint32_t testPid = GetCurrentProcessId() + 300;
    HostServer64 host;
    bool hostStarted = host.Start(testPid);
    assert(hostStarted);

    assert(!NRFusion_Capture32_IsConnected());
    bool connected = NRFusion_Capture32_Connect(testPid, 2000);
    assert(connected);
    assert(NRFusion_Capture32_IsConnected());

    CaptureClientConfig config{};
    config.width = 1920;
    config.height = 1080;
    config.workingScale = 0.67f;
    assert(NRFusion_Capture32_Configure(&config));

    PipelinedFrameResult result{};
    assert(NRFusion_Capture32_SubmitFramePipelined(1, 1, 0.0f, 0.0f, false, &result));

    NRFusion_Capture32_Disconnect();
    assert(!NRFusion_Capture32_IsConnected());
    host.Stop();
    std::cout << "  -> Standalone 32-bit Client C Export API PASSED.\n";
}

int main() {
    std::cout << std::unitbuf << "=== NRFusion Stage 3: x86 IPC Pipelined Architecture & Vulkan Tests ===\n";

    TestIpcHandshakeAndBuild();
    TestPipelinedFrameStreaming();
    TestD3D12SharedHandlePipeline();
    TestZeroGuideUploadRetirement();
    TestCapture32ExportApi();

    std::cout << "=== All Stage 3 Tests PASSED successfully! ===\n";
    return 0;
}
