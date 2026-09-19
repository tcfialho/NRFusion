#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/CaptureProvider32Export.h"

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct SpawnedHost {
    HANDLE process = nullptr;

    void Stop() {
        if (!process) return;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
        process = nullptr;
    }

    ~SpawnedHost() { Stop(); }
};

bool Fail(const char* message) {
    std::cerr << "[Capture32 Roundtrip] FAIL: " << message << "\n";
    return false;
}

std::string ResolveHostPath() {
    char configuredPath[MAX_PATH]{};
    const DWORD configuredLength = GetEnvironmentVariableA("NRFUSION_HOST64_PATH", configuredPath,
                                                            static_cast<DWORD>(sizeof(configuredPath)));
    if (configuredLength > 0 && configuredLength < sizeof(configuredPath)) return configuredPath;

    char executablePath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, executablePath, static_cast<DWORD>(sizeof(executablePath))) == 0) return {};
    std::string sibling(executablePath);
    const std::size_t separator = sibling.find_last_of("\\/");
    if (separator == std::string::npos) return "NRFusionHost64.exe";
    return sibling.substr(0, separator + 1) + "NRFusionHost64.exe";
}

bool StartHost(SpawnedHost& host) {
    const std::string hostPath = ResolveHostPath();
    if (hostPath.empty() || GetFileAttributesA(hostPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail("NRFusionHost64.exe was not found; set NRFUSION_HOST64_PATH for a cross-bitness run");
    }

    std::string commandLine = "\"" + hostPath + "\" " + std::to_string(GetCurrentProcessId());
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return Fail("CreateProcessA for NRFusionHost64.exe failed");
    }
    CloseHandle(process.hThread);
    host.process = process.hProcess;
    return true;
}

bool CreateSharedTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
                         bool allowUav, ComPtr<ID3D11Texture2D>& texture,
                         ComPtr<IDXGIKeyedMutex>& keyedMutex, HANDLE& sharedHandle) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
                            (allowUav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                            D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    const HRESULT createResult = device->CreateTexture2D(&description, nullptr, &texture);
    if (FAILED(createResult)) {
        std::cerr << "[Capture32 Roundtrip] CreateTexture2D(shared) failed: 0x" << std::hex
                  << static_cast<unsigned long>(createResult) << std::dec << "\n";
        return false;
    }

    ComPtr<IDXGIResource1> resource;
    const HRESULT queryResult = texture.As(&resource);
    const HRESULT mutexResult = SUCCEEDED(queryResult) ? texture.As(&keyedMutex) : queryResult;
    const HRESULT handleResult = SUCCEEDED(mutexResult)
        ? resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &sharedHandle) : queryResult;
    if (FAILED(handleResult)) {
        std::cerr << "[Capture32 Roundtrip] CreateSharedHandle(Color/Output) failed: 0x" << std::hex
                  << static_cast<unsigned long>(handleResult) << std::dec << "\n";
        return false;
    }
    return true;
}

bool CreateSharedTexture(ID3D11Device* device, UINT width, UINT height, ComPtr<ID3D11Texture2D>& texture,
                         ComPtr<IDXGIKeyedMutex>& keyedMutex, HANDLE& sharedHandle) {
    return CreateSharedTexture(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false,
                               texture, keyedMutex, sharedHandle);
}

bool CreateSharedFence(ID3D11Device5* device, ComPtr<ID3D11Fence>& fence, HANDLE& sharedHandle) {
    return SUCCEEDED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))) &&
        SUCCEEDED(fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &sharedHandle));
}

bool ReadFirstPixelForTest(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
                           std::array<uint8_t, 4>& pixel) {
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), source);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    pixel = {bytes[0], bytes[1], bytes[2], bytes[3]};
    context->Unmap(staging.Get(), 0);
    return true;
}

bool ReadFirstHalfPixelForTest(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source,
                               std::array<uint16_t, 4>& pixel) {
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), source);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* halves = static_cast<const uint16_t*>(mapped.pData);
    pixel = {halves[0], halves[1], halves[2], halves[3]};
    context->Unmap(staging.Get(), 0);
    return true;
}

bool SubmitUntilAccepted(uint64_t workId, uint64_t producerValue, uint64_t consumerValue,
                         nrfusion::PipelinedFrameResult& result) {
    for (int attempt = 0; attempt != 50; ++attempt) {
        if (NRFusion_Capture32_SubmitFramePipelinedEx(workId, 77, 9, producerValue, consumerValue,
                                                      0.0f, 0.0f, false, &result)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

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
        sourcePixel[0] != 64 || sourcePixel[1] != 128 || sourcePixel[2] != 191 || sourcePixel[3] != 255) {
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
    const bool copied = returnedPixel[0] == 64 && returnedPixel[1] == 128 &&
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
        return Fail("shared R16F Color/Output creation failed for Neural mode") ? 0 : 1;
    }

    ComPtr<ID3D11Fence> neuralInputFence;
    ComPtr<ID3D11Fence> neuralOutputFence;
    HANDLE neuralInputFenceHandle = nullptr;
    HANDLE neuralOutputFenceHandle = nullptr;
    if (!CreateSharedFence(device5.Get(), neuralInputFence, neuralInputFenceHandle) ||
        !CreateSharedFence(device5.Get(), neuralOutputFence, neuralOutputFenceHandle)) {
        return Fail("shared D3D11 fence creation failed for Neural mode") ? 0 : 1;
    }

    host.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!StartHost(host)) {
        return Fail("start host process for Neural mode failed") ? 0 : 1;
    }
    if (!NRFusion_Capture32_Connect(GetCurrentProcessId(), 5000)) {
        return Fail("connect to host for Neural mode failed") ? 0 : 1;
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
        return Fail("host rejected Neural mode configuration") ? 0 : 1;
    }

    ComPtr<ID3D11RenderTargetView> neuralColorRtv;
    if (FAILED(device->CreateRenderTargetView(neuralColor.Get(), nullptr, &neuralColorRtv))) {
        return Fail("Neural Color RTV creation failed") ? 0 : 1;
    }
    constexpr std::array<float, 4> neuralClearColor = {0.5f, 0.25f, 0.75f, 1.0f};
    if (neuralColorMutex->AcquireSync(0, 0) != S_OK) return Fail("neuralColor mutex unavailable") ? 0 : 1;
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
            return Fail("neural input fence signal failed") ? 0 : 1;
        }
        context->Flush();

        nrfusion::PipelinedFrameResult result{};
        if (!SubmitUntilAccepted(3000 + f, inFence, outFence, result)) {
            return Fail("neural frame submit failed") ? 0 : 1;
        }
        std::cout << "[Neural Frame " << f << "] hasResult=" << result.hasResult << " readyWork=" << result.readyWorkId << "\n";
        if (result.hasResult) {
            lastCompletedWork = result.readyWorkId;
            if (FAILED(context4->Wait(neuralOutputFence.Get(), result.completedFenceValue))) {
                return Fail("wait on neural consumer fence failed") ? 0 : 1;
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
        return Fail("Neural mode produced zero completed N-1 frames") ? 0 : 1;
    }

    std::array<uint16_t, 4> neuralOutputPixel{};
    if (neuralOutputMutex->AcquireSync(0, 0) != S_OK) return Fail("neural output mutex unavailable") ? 0 : 1;
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
        return Fail("Neural mode returned zero or unreadable pixels through shared Output") ? 0 : 1;
    }
    std::cout << "  -> Neural mode roundtrip PASSED (WorkId " << lastCompletedWork << " completed on GPU).\n";

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
        return Fail("shared R16F reduced Color/Output creation failed") ? 0 : 1;
    }

    ComPtr<ID3D11Fence> reducedInputFence;
    ComPtr<ID3D11Fence> reducedOutputFence;
    HANDLE reducedInputFenceHandle = nullptr;
    HANDLE reducedOutputFenceHandle = nullptr;
    if (!CreateSharedFence(device5.Get(), reducedInputFence, reducedInputFenceHandle) ||
        !CreateSharedFence(device5.Get(), reducedOutputFence, reducedOutputFenceHandle)) {
        return Fail("shared D3D11 fence creation failed for reduced scale") ? 0 : 1;
    }

    host.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!StartHost(host)) {
        return Fail("start host process for reduced scale failed") ? 0 : 1;
    }
    if (!NRFusion_Capture32_Connect(GetCurrentProcessId(), 5000)) {
        return Fail("connect to host for reduced scale failed") ? 0 : 1;
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
        return Fail("host rejected reduced scale configuration") ? 0 : 1;
    }

    ComPtr<ID3D11RenderTargetView> reducedColorRtv;
    if (FAILED(device->CreateRenderTargetView(reducedColor.Get(), nullptr, &reducedColorRtv))) {
        return Fail("reduced Color RTV creation failed") ? 0 : 1;
    }
    if (reducedColorMutex->AcquireSync(0, 0) != S_OK) return Fail("reduced color mutex unavailable") ? 0 : 1;
    context->ClearRenderTargetView(reducedColorRtv.Get(), neuralClearColor.data());
    context->Flush();
    reducedColorMutex->ReleaseSync(0);

    uint64_t lastReducedCompleted = 0;
    for (uint32_t f = 1; f <= 3; ++f) {
        const uint64_t inFence = 5000 + f;
        const uint64_t outFence = 6000 + f;
        if (FAILED(context4->Signal(reducedInputFence.Get(), inFence))) {
            return Fail("reduced input fence signal failed") ? 0 : 1;
        }
        context->Flush();

        nrfusion::PipelinedFrameResult result{};
        if (!SubmitUntilAccepted(5000 + f, inFence, outFence, result)) {
            return Fail("reduced frame submit failed") ? 0 : 1;
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
        return Fail("reduced WorkingScale produced zero completed N-1 frames") ? 0 : 1;
    }

    std::array<uint16_t, 4> reducedOutputPixel{};
    if (reducedOutputMutex->AcquireSync(0, 0) != S_OK) return Fail("reduced output mutex unavailable") ? 0 : 1;
    const bool reducedReadOk = ReadFirstHalfPixelForTest(device.Get(), context.Get(), reducedOutput.Get(), reducedOutputPixel);
    reducedOutputMutex->ReleaseSync(0);

    NRFusion_Capture32_Disconnect();
    CloseHandle(reducedColorHandle);
    CloseHandle(reducedOutputHandle);
    CloseHandle(reducedInputFenceHandle);
    CloseHandle(reducedOutputFenceHandle);

    if (!reducedReadOk) {
        return Fail("failed to read back reduced output texture") ? 0 : 1;
    }
    std::cout << "  -> Reduced scale roundtrip PASSED (WorkId " << lastReducedCompleted << " completed on GPU).\n";

    std::cout << "[Capture32 Roundtrip] PASS: cross-process handles, GPU fences, N-1 output, host restart and Neural mode verified.\n";
    host.Stop();
    return 0;
}
