#include "Capture32RoundtripSupport.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

bool RunNeuralRoundtrip(
    SpawnedHost& host,
    const ComPtr<ID3D11Device>& device,
    const ComPtr<ID3D11Device5>& device5,
    const ComPtr<ID3D11DeviceContext>& context,
    const ComPtr<ID3D11DeviceContext4>& context4) {
    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    // Phase 2: Neural processing mode round-trip (WorkingScale == 1.0f, R16G16B16A16_FLOAT)
    std::cout << "[Capture32 Roundtrip] Testing IpcProcessingMode::Neural at WorkingScale 1.0...\n";
    ComPtr<ID3D11Texture2D> neuralColor;
    ComPtr<ID3D11Texture2D> neuralOutput;
    ComPtr<IDXGIKeyedMutex> neuralColorMutex;
    ComPtr<IDXGIKeyedMutex> neuralOutputMutex;
    HANDLE neuralColorHandle = nullptr;
    HANDLE neuralOutputHandle = nullptr;
    if (!CreateSharedTexture(device.Get(), kWidth, kHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                             neuralColor, neuralColorMutex, neuralColorHandle) ||
        !CreateSharedTexture(device.Get(), kWidth, kHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, true,
                             neuralOutput, neuralOutputMutex, neuralOutputHandle)) {
        return Fail("shared R16F Color/Output creation failed for Neural mode");
    }

    ComPtr<ID3D11Fence> neuralInputFence;
    ComPtr<ID3D11Fence> neuralOutputFence;
    HANDLE neuralInputFenceHandle = nullptr;
    HANDLE neuralOutputFenceHandle = nullptr;
    if (!CreateSharedFence(device5.Get(), neuralInputFence, neuralInputFenceHandle) ||
        !CreateSharedFence(device5.Get(), neuralOutputFence, neuralOutputFenceHandle)) {
        return Fail("shared D3D11 fence creation failed for Neural mode");
    }

    host.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!StartHost(host)) {
        return Fail("start host process for Neural mode failed");
    }
    if (!NRFusion_Capture32_Connect(GetCurrentProcessId(), 5000)) {
        return Fail("connect to host for Neural mode failed");
    }

    nrfusion::CaptureClientConfig neuralConfig{};
    neuralConfig.width = kWidth;
    neuralConfig.height = kHeight;
    neuralConfig.targetWidth = kWidth;
    neuralConfig.targetHeight = kHeight;
    neuralConfig.workingScale = 1.0f;
    neuralConfig.colorFormat = static_cast<uint32_t>(DXGI_FORMAT_R16G16B16A16_FLOAT);
    neuralConfig.processingMode = nrfusion::IpcProcessingMode::Neural;
    neuralConfig.colorSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(neuralColorHandle));
    neuralConfig.residualSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(neuralOutputHandle));
    neuralConfig.producerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(neuralInputFenceHandle));
    neuralConfig.consumerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(neuralOutputFenceHandle));

    if (!NRFusion_Capture32_Configure(&neuralConfig)) {
        return Fail("host rejected Neural mode configuration");
    }

    ComPtr<ID3D11RenderTargetView> neuralColorRtv;
    if (FAILED(device->CreateRenderTargetView(neuralColor.Get(), nullptr, &neuralColorRtv))) {
        return Fail("Neural Color RTV creation failed");
    }
    constexpr std::array<float, 4> neuralClearColor = {0.5f, 0.25f, 0.75f, 1.0f};
    if (neuralColorMutex->AcquireSync(0, 0) != S_OK) return Fail("neuralColor mutex unavailable");
    context->ClearRenderTargetView(neuralColorRtv.Get(), neuralClearColor.data());
    context->Flush();
    neuralColorMutex->ReleaseSync(0);

    std::array<uint16_t, 4> sourceHalf{};
    if (neuralColorMutex->AcquireSync(0, 0) == S_OK) {
        ReadFirstHalfPixelForTest(device.Get(), context.Get(), neuralColor.Get(), sourceHalf);
        neuralColorMutex->ReleaseSync(0);
        std::cout << "[Neural Source] r=" << sourceHalf[0] << " g=" << sourceHalf[1]
                  << " b=" << sourceHalf[2] << " a=" << sourceHalf[3] << "\n";
    }

    constexpr uint32_t kNeuralFrames = 5;
    uint64_t lastCompletedWork = 0;
    for (uint32_t f = 1; f <= kNeuralFrames; ++f) {
        const uint64_t inFence = 3000 + f;
        const uint64_t outFence = 4000 + f;
        if (FAILED(context4->Signal(neuralInputFence.Get(), inFence))) {
            return Fail("neural input fence signal failed");
        }
        context->Flush();

        nrfusion::PipelinedFrameResult result{};
        if (!SubmitUntilAccepted(3000 + f, inFence, outFence, result)) {
            return Fail("neural frame submit failed");
        }
        std::cout << "[Neural Frame " << f << "] hasResult=" << result.hasResult << " readyWork=" << result.readyWorkId << "\n";
        if (result.hasResult) {
            lastCompletedWork = result.readyWorkId;
            if (FAILED(context4->Wait(neuralOutputFence.Get(), result.completedFenceValue))) {
                return Fail("wait on neural consumer fence failed");
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (int drainAttempt = 0; drainAttempt < 10 && lastCompletedWork == 0; ++drainAttempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const uint64_t drainIn = 3000 + kNeuralFrames + 1 + drainAttempt;
        const uint64_t drainOut = 4000 + kNeuralFrames + 1 + drainAttempt;
        context4->Signal(neuralInputFence.Get(), drainIn);
        context->Flush();
        nrfusion::PipelinedFrameResult drainResult{};
        if (SubmitUntilAccepted(drainIn, drainIn, drainOut, drainResult)) {
            std::cout << "[Neural Drain " << drainAttempt << "] hasResult=" << drainResult.hasResult
                      << " readyWork=" << drainResult.readyWorkId << "\n";
            if (drainResult.hasResult) {
                lastCompletedWork = drainResult.readyWorkId;
                context4->Wait(neuralOutputFence.Get(), drainResult.completedFenceValue);
                break;
            }
        }
    }

    if (lastCompletedWork == 0) {
        return Fail("Neural mode produced zero completed N-1 frames");
    }

    std::array<uint16_t, 4> neuralOutputPixel{};
    if (neuralOutputMutex->AcquireSync(0, 0) != S_OK) return Fail("neural output mutex unavailable");
    const bool neuralReadOk = ReadFirstHalfPixelForTest(device.Get(), context.Get(), neuralOutput.Get(), neuralOutputPixel);
    neuralOutputMutex->ReleaseSync(0);

    std::cout << "[Neural Readback] ok=" << neuralReadOk
              << " r=" << neuralOutputPixel[0] << " g=" << neuralOutputPixel[1]
              << " b=" << neuralOutputPixel[2] << " a=" << neuralOutputPixel[3] << "\n";

    NRFusion_Capture32_Disconnect();
    CloseHandle(neuralColorHandle);
    CloseHandle(neuralOutputHandle);
    CloseHandle(neuralInputFenceHandle);
    CloseHandle(neuralOutputFenceHandle);

    if (!neuralReadOk || (neuralOutputPixel[0] == 0 && neuralOutputPixel[1] == 0 && neuralOutputPixel[2] == 0)) {
        return Fail("Neural mode returned zero or unreadable pixels through shared Output");
    }
    std::cout << "  -> Neural mode roundtrip PASSED (WorkId " << lastCompletedWork << " completed on GPU).\n";

    return true;
}
