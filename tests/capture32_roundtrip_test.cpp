#include "Capture32RoundtripSupport.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

using Microsoft::WRL::ComPtr;

int main() {
    std::cout << "[Capture32 Roundtrip] D3D11 shared Color/Output + cross-process host test\n";

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &device, &featureLevel, &context))) {
        return Fail("D3D11 hardware device creation failed") ? 0 : 1;
    }

    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    if (FAILED(device.As(&device5)) || FAILED(context.As(&context4))) {
        return Fail("ID3D11Device5/ID3D11DeviceContext4 shared-fence support is unavailable") ? 0 : 1;
    }

    constexpr UINT kWidth = 64;
    constexpr UINT kHeight = 64;
    ComPtr<ID3D11Texture2D> sharedColor;
    ComPtr<ID3D11Texture2D> sharedOutput;
    ComPtr<IDXGIKeyedMutex> colorMutex;
    ComPtr<IDXGIKeyedMutex> outputMutex;
    HANDLE colorHandle = nullptr;
    HANDLE outputHandle = nullptr;
    if (!CreateSharedTexture(device.Get(), kWidth, kHeight, sharedColor, colorMutex, colorHandle) ||
        !CreateSharedTexture(device.Get(), kWidth, kHeight, sharedOutput, outputMutex, outputHandle)) {
        return Fail("shared D3D11 Color/Output creation failed") ? 0 : 1;
    }

    ComPtr<ID3D11Fence> inputFence;
    ComPtr<ID3D11Fence> outputFence;
    HANDLE inputFenceHandle = nullptr;
    HANDLE outputFenceHandle = nullptr;
    if (!CreateSharedFence(device5.Get(), inputFence, inputFenceHandle) ||
        !CreateSharedFence(device5.Get(), outputFence, outputFenceHandle)) {
        return Fail("shared D3D11 fence creation failed") ? 0 : 1;
    }

    SpawnedHost host;
    if (!StartHost(host)) return 1;

    if (!NRFusion_Capture32_Connect(GetCurrentProcessId(), 3000)) return Fail("capture client could not connect") ? 0 : 1;

    nrfusion::CaptureClientConfig config{};
    config.width = kWidth;
    config.height = kHeight;
    config.workingScale = 1.0f;
    config.colorFormat = static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    config.processingMode = nrfusion::IpcProcessingMode::DummyCopy;
    config.colorSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(colorHandle));
    config.residualSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(outputHandle));
    config.producerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(inputFenceHandle));
    config.consumerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(outputFenceHandle));
    if (!NRFusion_Capture32_Configure(&config)) return Fail("host rejected duplicated shared handles") ? 0 : 1;

    ComPtr<ID3D11RenderTargetView> colorView;
    if (FAILED(device->CreateRenderTargetView(sharedColor.Get(), nullptr, &colorView))) {
        return Fail("Color RTV creation failed") ? 0 : 1;
    }
    constexpr std::array<float, 4> expectedColor = {0.25f, 0.5f, 0.75f, 1.0f};
    if (colorMutex->AcquireSync(0, 0) != S_OK) return Fail("source keyed mutex was unavailable") ? 0 : 1;
    context->ClearRenderTargetView(colorView.Get(), expectedColor.data());
    std::array<uint8_t, 4> sourcePixel{};
    colorMutex->ReleaseSync(0);
    if (colorMutex->AcquireSync(0, 0) != S_OK) return Fail("source keyed mutex could not be reacquired") ? 0 : 1;
    const bool sourceVerified = ReadFirstPixelForTest(device.Get(), context.Get(), sharedColor.Get(), sourcePixel);
    colorMutex->ReleaseSync(0);
    if (!sourceVerified ||
        sourcePixel[0] != 64 || sourcePixel[1] < 127 || sourcePixel[1] > 128 ||
        sourcePixel[2] != 191 || sourcePixel[3] != 255) {
        std::cerr << "[Capture32 Roundtrip] source pixel = " << static_cast<unsigned>(sourcePixel[0]) << ','
                  << static_cast<unsigned>(sourcePixel[1]) << ',' << static_cast<unsigned>(sourcePixel[2]) << ','
                  << static_cast<unsigned>(sourcePixel[3]) << "\n";
        return Fail("D3D11 Color write did not reach the shared source texture") ? 0 : 1;
    }

    constexpr uint64_t kInputFirst = 41;
    constexpr uint64_t kOutputFirst = 501;
    if (FAILED(context4->Signal(inputFence.Get(), kInputFirst))) return Fail("input fence signal failed") ? 0 : 1;
    context->Flush();

    nrfusion::PipelinedFrameResult firstResult{};
    if (!SubmitUntilAccepted(1001, kInputFirst, kOutputFirst, firstResult)) {
        return Fail("first IPC frame submission failed") ? 0 : 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    constexpr uint64_t kInputSecond = 42;
    constexpr uint64_t kOutputSecond = 502;
    if (FAILED(context4->Signal(inputFence.Get(), kInputSecond))) return Fail("second input fence signal failed") ? 0 : 1;
    context->Flush();

    nrfusion::PipelinedFrameResult completed{};
    if (!SubmitUntilAccepted(1002, kInputSecond, kOutputSecond, completed)) {
        return Fail("second IPC frame submission failed") ? 0 : 1;
    }
    if (!completed.hasResult || completed.readyWorkId != 1001 || completed.completedFenceValue != kOutputFirst) {
        return Fail("N-1 acknowledgement did not preserve independent WorkId and fence timelines") ? 0 : 1;
    }

    if (FAILED(context4->Wait(outputFence.Get(), completed.completedFenceValue))) {
        return Fail("D3D11 GPU wait on host output fence failed") ? 0 : 1;
    }

    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width = kWidth;
    stagingDesc.Height = kHeight;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &readback))) {
        return Fail("test-only readback texture creation failed") ? 0 : 1;
    }

    if (outputMutex->AcquireSync(0, 0) != S_OK) return Fail("output keyed mutex was unavailable") ? 0 : 1;
    context->CopyResource(readback.Get(), sharedOutput.Get());
    context->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return Fail("test-only readback map failed") ? 0 : 1;
    }
    const auto* pixel = static_cast<const uint8_t*>(mapped.pData);
    const std::array<uint8_t, 4> returnedPixel = {pixel[0], pixel[1], pixel[2], pixel[3]};
    const bool copied = returnedPixel[0] == 64 &&
        returnedPixel[1] >= 127 && returnedPixel[1] <= 128 &&
        returnedPixel[2] == 191 && returnedPixel[3] == 255;
    context->Unmap(readback.Get(), 0);
    outputMutex->ReleaseSync(0);

    if (!copied) {
        std::cerr << "[Capture32 Roundtrip] returned pixel = " << static_cast<unsigned>(returnedPixel[0]) << ','
                  << static_cast<unsigned>(returnedPixel[1]) << ',' << static_cast<unsigned>(returnedPixel[2]) << ','
                  << static_cast<unsigned>(returnedPixel[3]) << "\n";
        return Fail("host dummy copy did not return the Color pixels through shared Output") ? 0 : 1;
    }

    const uint64_t firstSession = NRFusion_Capture32_ActiveSessionId();
    const uint64_t firstGeneration = NRFusion_Capture32_ActiveConnectionGeneration();
    if (firstGeneration == 0) return Fail("initial connection generation was zero") ? 0 : 1;
    host.Stop();

    // Do not pre-disconnect: the next IPC operation must observe the dead host and invalidate its
    // process handle before a replacement host receives a new DuplicateHandle set.
    if (FAILED(context4->Signal(inputFence.Get(), 61))) {
        return Fail("restart-fault input fence signal failed") ? 0 : 1;
    }
    context->Flush();
    bool faultDetected = false;
    for (int attempt = 0; attempt != 20; ++attempt) {
        nrfusion::PipelinedFrameResult faultResult{};
        NRFusion_Capture32_SubmitFramePipelinedEx(1501, 77, 9, 61, 651, 0.0f, 0.0f, false, &faultResult);
        if (!NRFusion_Capture32_IsConnected()) {
            faultDetected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!faultDetected) return Fail("capture client did not invalidate the dead host transport") ? 0 : 1;

    if (!StartHost(host) || !NRFusion_Capture32_Connect(GetCurrentProcessId(), 3000) ||
        !NRFusion_Capture32_Configure(&config)) {
        return Fail("host restart could not establish a fresh shared-handle session") ? 0 : 1;
    }
    if (NRFusion_Capture32_ActiveSessionId() <= firstSession) {
        return Fail("host restart did not advance the capture session") ? 0 : 1;
    }
    const uint64_t restartedGeneration =
        NRFusion_Capture32_ActiveConnectionGeneration();
    if (restartedGeneration == 0 || restartedGeneration == firstGeneration) {
        return Fail("host restart did not establish a fresh connection generation") ? 0 : 1;
    }

    constexpr std::array<float, 4> restartedColor = {0.0f, 1.0f, 0.0f, 1.0f};
    if (colorMutex->AcquireSync(0, 0) != S_OK) return Fail("source keyed mutex failed after host restart") ? 0 : 1;
    context->ClearRenderTargetView(colorView.Get(), restartedColor.data());
    colorMutex->ReleaseSync(0);

    constexpr uint64_t kInputRestartFirst = 81;
    constexpr uint64_t kOutputRestartFirst = 701;
    constexpr uint64_t kInputRestartSecond = 82;
    constexpr uint64_t kOutputRestartSecond = 702;
    if (FAILED(context4->Signal(inputFence.Get(), kInputRestartFirst))) {
        return Fail("restart input fence signal failed") ? 0 : 1;
    }
    context->Flush();
    nrfusion::PipelinedFrameResult restartFirst{};
    if (!SubmitUntilAccepted(2001, kInputRestartFirst, kOutputRestartFirst, restartFirst)) {
        return Fail("restart first frame submission failed") ? 0 : 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (FAILED(context4->Signal(inputFence.Get(), kInputRestartSecond))) {
        return Fail("restart second input fence signal failed") ? 0 : 1;
    }
    context->Flush();
    nrfusion::PipelinedFrameResult restartCompleted{};
    if (!SubmitUntilAccepted(2002, kInputRestartSecond, kOutputRestartSecond, restartCompleted) ||
        !restartCompleted.hasResult || restartCompleted.readyWorkId != 2001 ||
        restartCompleted.completedFenceValue != kOutputRestartFirst) {
        return Fail("restart did not return the new-session N-1 result") ? 0 : 1;
    }

    if (FAILED(context4->Wait(outputFence.Get(), restartCompleted.completedFenceValue)) ||
        outputMutex->AcquireSync(0, 0) != S_OK) {
        return Fail("restart output fence or keyed mutex failed") ? 0 : 1;
    }
    std::array<uint8_t, 4> restartPixel{};
    const bool restartRead = ReadFirstPixelForTest(device.Get(), context.Get(), sharedOutput.Get(), restartPixel);
    outputMutex->ReleaseSync(0);
    NRFusion_Capture32_Disconnect();

    CloseHandle(colorHandle);
    CloseHandle(outputHandle);
    CloseHandle(inputFenceHandle);
    CloseHandle(outputFenceHandle);

    if (!restartRead || restartPixel[0] != 0 || restartPixel[1] != 255 ||
        restartPixel[2] != 0 || restartPixel[3] != 255) {
        return Fail("restart output did not come from the new host session") ? 0 : 1;
    }

    if (!RunNeuralRoundtrip(host, device, device5, context, context4))
        return 1;
    if (!RunReducedRoundtrip(host, device, device5, context, context4))
        return 1;

    std::cout << "[Capture32 Roundtrip] PASS: cross-process handles, GPU fences, N-1 output, host restart and Neural mode verified.\n";
    host.Stop();
    return 0;
}
