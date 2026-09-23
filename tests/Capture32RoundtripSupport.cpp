#include "Capture32RoundtripSupport.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

void SpawnedHost::Stop() {
    if (!process) return;
    TerminateProcess(process, 0);
    WaitForSingleObject(process, 2000);
    CloseHandle(process);
    process = nullptr;
}

SpawnedHost::~SpawnedHost() {
    Stop();
}

bool Fail(const char* message) {
    std::cerr << "[Capture32 Roundtrip] FAIL: " << message << "\n";
    return false;
}

namespace {

std::string ResolveHostPath() {
    char configuredPath[MAX_PATH]{};
    const DWORD configuredLength = GetEnvironmentVariableA(
        "NRFUSION_HOST64_PATH", configuredPath,
        static_cast<DWORD>(sizeof(configuredPath)));
    if (configuredLength > 0 && configuredLength < sizeof(configuredPath))
        return configuredPath;

    char executablePath[MAX_PATH]{};
    if (GetModuleFileNameA(
            nullptr, executablePath,
            static_cast<DWORD>(sizeof(executablePath))) == 0)
        return {};
    std::string sibling(executablePath);
    const std::size_t separator = sibling.find_last_of("\\/");
    if (separator == std::string::npos) return "NRFusionHost64.exe";
    return sibling.substr(0, separator + 1) + "NRFusionHost64.exe";
}

} // namespace

bool StartHost(SpawnedHost& host) {
    const std::string hostPath = ResolveHostPath();
    if (hostPath.empty() ||
        GetFileAttributesA(hostPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail(
            "NRFusionHost64.exe was not found; set NRFUSION_HOST64_PATH for a cross-bitness run");
    }

    std::string commandLine =
        "\"" + hostPath + "\" " + std::to_string(GetCurrentProcessId());
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(
            nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return Fail("CreateProcessA for NRFusionHost64.exe failed");
    }
    CloseHandle(process.hThread);
    host.process = process.hProcess;
    return true;
}

bool CreateSharedTexture(
    ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
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
    description.BindFlags =
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
        (allowUav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
    description.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    const HRESULT createResult =
        device->CreateTexture2D(&description, nullptr, &texture);
    if (FAILED(createResult)) {
        std::cerr
            << "[Capture32 Roundtrip] CreateTexture2D(shared) failed: 0x"
            << std::hex << static_cast<unsigned long>(createResult)
            << std::dec << "\n";
        return false;
    }

    ComPtr<IDXGIResource1> resource;
    const HRESULT queryResult = texture.As(&resource);
    const HRESULT mutexResult =
        SUCCEEDED(queryResult) ? texture.As(&keyedMutex) : queryResult;
    const HRESULT handleResult = SUCCEEDED(mutexResult)
        ? resource->CreateSharedHandle(
              nullptr, GENERIC_ALL, nullptr, &sharedHandle)
        : queryResult;
    if (FAILED(handleResult)) {
        std::cerr
            << "[Capture32 Roundtrip] CreateSharedHandle(Color/Output) failed: 0x"
            << std::hex << static_cast<unsigned long>(handleResult)
            << std::dec << "\n";
        return false;
    }
    return true;
}

bool CreateSharedTexture(
    ID3D11Device* device, UINT width, UINT height,
    ComPtr<ID3D11Texture2D>& texture,
    ComPtr<IDXGIKeyedMutex>& keyedMutex, HANDLE& sharedHandle) {
    return CreateSharedTexture(
        device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, false,
        texture, keyedMutex, sharedHandle);
}

bool CreateSharedFence(
    ID3D11Device5* device, ComPtr<ID3D11Fence>& fence,
    HANDLE& sharedHandle) {
    return SUCCEEDED(device->CreateFence(
               0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence))) &&
           SUCCEEDED(fence->CreateSharedHandle(
               nullptr, GENERIC_ALL, nullptr, &sharedHandle));
}

bool ReadFirstPixelForTest(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* source, std::array<std::uint8_t, 4>& pixel) {
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(
            &description, nullptr, &staging)))
        return false;
    context->CopyResource(staging.Get(), source);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(
            staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto* bytes = static_cast<const std::uint8_t*>(mapped.pData);
    pixel = {bytes[0], bytes[1], bytes[2], bytes[3]};
    context->Unmap(staging.Get(), 0);
    return true;
}

bool ReadFirstHalfPixelForTest(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* source, std::array<std::uint16_t, 4>& pixel) {
    D3D11_TEXTURE2D_DESC description{};
    source->GetDesc(&description);
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(
            &description, nullptr, &staging)))
        return false;
    context->CopyResource(staging.Get(), source);
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(
            staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;
    const auto* halves =
        static_cast<const std::uint16_t*>(mapped.pData);
    pixel = {halves[0], halves[1], halves[2], halves[3]};
    context->Unmap(staging.Get(), 0);
    return true;
}

bool SubmitUntilAccepted(
    std::uint64_t workId, std::uint64_t producerValue,
    std::uint64_t consumerValue, nrfusion::PipelinedFrameResult& result) {
    for (int attempt = 0; attempt != 50; ++attempt) {
        if (NRFusion_Capture32_SubmitFramePipelinedEx(
                workId, 77, 9, producerValue, consumerValue,
                0.0f, 0.0f, false, &result))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
