#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>

#include "NrKernelRuntimeIdentity.hpp"
#include "nrfusion/Sha256.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace nrfusion::kernelprofile {

bool WriteRuntimeIdentity(const char* csvPath, void* submissionQueue) {
    std::array<wchar_t, 32768> path{};
    const auto runtime = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (!runtime || !GetModuleFileNameW(runtime, path.data(), static_cast<DWORD>(path.size())))
        return false;
    const std::filesystem::path runtimePath(path.data());
    const auto hash = Sha256File(runtimePath);
    if (!hash) return false;
    const auto utf8 = runtimePath.u8string();
    const std::string runtimeName(utf8.begin(), utf8.end());
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.data(), &ignored);
    std::vector<unsigned char> version(size);
    VS_FIXEDFILEINFO* fileVersion = nullptr;
    UINT versionBytes = 0;
    std::string runtimeVersion;
    if (size && GetFileVersionInfoW(path.data(), 0, size, version.data()) &&
        VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&fileVersion), &versionBytes) &&
        versionBytes >= sizeof(*fileVersion)) {
        runtimeVersion = std::to_string(HIWORD(fileVersion->dwFileVersionMS)) + "." +
            std::to_string(LOWORD(fileVersion->dwFileVersionMS)) + "." +
            std::to_string(HIWORD(fileVersion->dwFileVersionLS)) + "." +
            std::to_string(LOWORD(fileVersion->dwFileVersionLS));
    }
    int driverVersion = 0;
    const auto driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using DriverVersion = int(__stdcall*)(int*);
    const auto getVersion = driver
        ? reinterpret_cast<DriverVersion>(GetProcAddress(driver, "cuDriverGetVersion")) : nullptr;
    const bool driverKnown = getVersion && getVersion(&driverVersion) == 0;
    using DriverInit = int(__stdcall*)(unsigned);
    using DeviceCount = int(__stdcall*)(int*);
    using DeviceLuid = int(__stdcall*)(char*, unsigned*, int);
    using DeviceAttribute = int(__stdcall*)(int*, int, int);
    const auto init = driver ? reinterpret_cast<DriverInit>(GetProcAddress(driver, "cuInit")) : nullptr;
    const auto countDevices = driver ? reinterpret_cast<DeviceCount>(GetProcAddress(driver, "cuDeviceGetCount")) : nullptr;
    const auto deviceLuid = driver ? reinterpret_cast<DeviceLuid>(GetProcAddress(driver, "cuDeviceGetLuid")) : nullptr;
    const auto attribute = driver ? reinterpret_cast<DeviceAttribute>(GetProcAddress(driver, "cuDeviceGetAttribute")) : nullptr;
    int major = 0, minor = 0, devices = 0;
    constexpr int computeMajor = 75, computeMinor = 76;
    auto* queue = static_cast<ID3D12CommandQueue*>(submissionQueue);
    ID3D12Device* device = nullptr;
    if (queue && SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device)))) {
        const auto luid = device->GetAdapterLuid();
        if (init && countDevices && deviceLuid && attribute && init(0) == 0 && countDevices(&devices) == 0) {
            for (int ordinal = 0; ordinal < devices; ++ordinal) {
                std::array<char, sizeof(LUID)> candidate{};
                unsigned nodeMask = 0;
                if (deviceLuid(candidate.data(), &nodeMask, ordinal) == 0 &&
                    std::memcmp(candidate.data(), &luid, sizeof(luid)) == 0) {
                    if (attribute(&major, computeMajor, ordinal) != 0 || attribute(&minor, computeMinor, ordinal) != 0)
                        major = minor = 0;
                    break;
                }
            }
        }
        device->Release();
    }
    if (driver) FreeLibrary(driver);
    std::ofstream output(std::string(csvPath) + ".metadata.json");
    output << "{\n  \"schema_version\": 1,\n  \"backend\": \"nvapi_d3d12\",\n"
        << "  \"runtime_path\": " << std::quoted(runtimeName) << ",\n"
        << "  \"runtime_sha256\": " << std::quoted(*hash) << ",\n"
        << "  \"runtime_file_version\": " << std::quoted(runtimeVersion) << ",\n"
        << "  \"nvapi_sdk_commit\": \"70d337db9186e968eab622f7e786de7e437faf3d\",\n"
        << "  \"stream_semantics\": \"D3D12 command-list and submission-queue ordering; no CUstream\",\n"
        << "  \"timing_scope\": \"complete NVAPI chain; gpu_ms only for single-kernel chains\",\n"
        << "  \"cuda_driver_version\": ";
    if (driverKnown) output << driverVersion;
    else output << "null";
    output << ",\n  \"gpu_architecture\": ";
    if (major) output << std::quoted("sm_" + std::to_string(major) + std::to_string(minor));
    else output << "null";
    output << "\n}\n";
    return static_cast<bool>(output);
}

} // namespace nrfusion::kernelprofile
