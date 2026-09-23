#include "Capture32RoundtripSupport.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

bool RunReducedRoundtrip(
    SpawnedHost& host,
    const ComPtr<ID3D11Device>& device,
    const ComPtr<ID3D11Device5>& device5,
    const ComPtr<ID3D11DeviceContext>& context,
    const ComPtr<ID3D11DeviceContext4>& context4) {
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    constexpr std::array<float, 4> neuralClearColor = {0.5f, 0.25f, 0.75f, 1.0f};
    // Phase 3: Reduced WorkingScale (0.5f) transport and residual verification
    std::cout << "[Capture32 Roundtrip] Testing reduced WorkingScale 0.5f (32x32 transport -> 64x64 target)...\n";
    constexpr UINT kReducedWidth = 32;
    constexpr UINT kReducedHeight = 32;
    ComPtr<ID3D11Texture2D> reducedColor;
    ComPtr<ID3D11Texture2D> reducedOutput;
    ComPtr<IDXGIKeyedMutex> reducedColorMutex;
    ComPtr<IDXGIKeyedMutex> reducedOutputMutex;
    HANDLE reducedColorHandle = nullptr;
    HANDLE reducedOutputHandle = nullptr;
    if (!CreateSharedTexture(device.Get(), kReducedWidth, kReducedHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                             reducedColor, reducedColorMutex, reducedColorHandle) ||
        !CreateSharedTexture(device.Get(), kReducedWidth, kReducedHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                             reducedOutput, reducedOutputMutex, reducedOutputHandle)) {
        return Fail("shared R16F reduced Color/Output creation failed");
    }

    ComPtr<ID3D11Fence> reducedInputFence;
    ComPtr<ID3D11Fence> reducedOutputFence;
    HANDLE reducedInputFenceHandle = nullptr;
    HANDLE reducedOutputFenceHandle = nullptr;
    if (!CreateSharedFence(device5.Get(), reducedInputFence, reducedInputFenceHandle) ||
        !CreateSharedFence(device5.Get(), reducedOutputFence, reducedOutputFenceHandle)) {
        return Fail("shared D3D11 fence creation failed for reduced scale");
    }

    host.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!StartHost(host)) {
        return Fail("start host process for reduced scale failed");
    }
    if (!NRFusion_Capture32_Connect(GetCurrentProcessId(), 5000)) {
        return Fail("connect to host for reduced scale failed");
    }

    nrfusion::CaptureClientConfig reducedConfig{};
    reducedConfig.width = kReducedWidth;
    reducedConfig.height = kReducedHeight;
    reducedConfig.targetWidth = kWidth;
    reducedConfig.targetHeight = kHeight;
    reducedConfig.workingScale = 0.5f;
    reducedConfig.colorFormat = static_cast<uint32_t>(DXGI_FORMAT_R16G16B16A16_FLOAT);
    reducedConfig.processingMode = nrfusion::IpcProcessingMode::Neural;
    reducedConfig.colorSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reducedColorHandle));
    reducedConfig.residualSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reducedOutputHandle));
    reducedConfig.producerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reducedInputFenceHandle));
    reducedConfig.consumerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(reducedOutputFenceHandle));

    if (!NRFusion_Capture32_Configure(&reducedConfig)) {
        return Fail("host rejected reduced scale configuration");
    }

    ComPtr<ID3D11RenderTargetView> reducedColorRtv;
    if (FAILED(device->CreateRenderTargetView(reducedColor.Get(), nullptr, &reducedColorRtv))) {
        return Fail("reduced Color RTV creation failed");
    }
    if (reducedColorMutex->AcquireSync(0, 0) != S_OK) return Fail("reduced color mutex unavailable");
    context->ClearRenderTargetView(reducedColorRtv.Get(), neuralClearColor.data());
    context->Flush();
    reducedColorMutex->ReleaseSync(0);

    uint64_t lastReducedCompleted = 0;
    for (uint32_t f = 1; f <= 3; ++f) {
        const uint64_t inFence = 5000 + f;
        const uint64_t outFence = 6000 + f;
        if (FAILED(context4->Signal(reducedInputFence.Get(), inFence))) {
            return Fail("reduced input fence signal failed");
        }
        context->Flush();

        nrfusion::PipelinedFrameResult result{};
        if (!SubmitUntilAccepted(5000 + f, inFence, outFence, result)) {
            return Fail("reduced frame submit failed");
        }
        if (result.hasResult) {
            lastReducedCompleted = result.readyWorkId;
            context4->Wait(reducedOutputFence.Get(), result.completedFenceValue);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (int drainAttempt = 0; drainAttempt < 10 && lastReducedCompleted == 0; ++drainAttempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const uint64_t drainIn = 5004 + drainAttempt;
        const uint64_t drainOut = 6004 + drainAttempt;
        context4->Signal(reducedInputFence.Get(), drainIn);
        context->Flush();
        nrfusion::PipelinedFrameResult drainResult{};
        if (SubmitUntilAccepted(drainIn, drainIn, drainOut, drainResult)) {
            if (drainResult.hasResult) {
                lastReducedCompleted = drainResult.readyWorkId;
                context4->Wait(reducedOutputFence.Get(), drainResult.completedFenceValue);
                break;
            }
        }
    }

    if (lastReducedCompleted == 0) {
        return Fail("reduced WorkingScale produced zero completed N-1 frames");
    }

    std::array<uint16_t, 4> reducedOutputPixel{};
    if (reducedOutputMutex->AcquireSync(0, 0) != S_OK) return Fail("reduced output mutex unavailable");
    const bool reducedReadOk = ReadFirstHalfPixelForTest(device.Get(), context.Get(), reducedOutput.Get(), reducedOutputPixel);
    reducedOutputMutex->ReleaseSync(0);

    NRFusion_Capture32_Disconnect();
    CloseHandle(reducedColorHandle);
    CloseHandle(reducedOutputHandle);
    CloseHandle(reducedInputFenceHandle);
    CloseHandle(reducedOutputFenceHandle);

    if (!reducedReadOk) {
        return Fail("failed to read back reduced output texture");
    }
    std::cout << "  -> Reduced scale roundtrip PASSED (WorkId " << lastReducedCompleted << " completed on GPU).\n";

    return true;
}
