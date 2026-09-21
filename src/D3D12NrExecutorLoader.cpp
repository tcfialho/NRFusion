#include "nrfusion/D3D12NrExecutor.hpp"

#include <array>

namespace nrfusion {
namespace {

constexpr std::array<const wchar_t*, 3> kDriverCandidates = { L"nvngx.dll", L"_nvngx.dll", L"nvngx_dlss.dll" };
constexpr int kNgxSuccess = 1;

template <typename T>
T Symbol(HMODULE module, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    const auto slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : dir.substr(0, slash + 1);
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Standalone hosts may need DriverStore because nvngx.dll is outside the normal DLL search path.
HMODULE LoadDriverNgxFromDriverStore() {
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0) return nullptr;

    std::wstring pattern = std::wstring(systemDir) + L"\\DriverStore\\FileRepository\\*";
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return nullptr;

    HMODULE found = nullptr;
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) continue;

        std::wstring candidate = std::wstring(systemDir) + L"\\DriverStore\\FileRepository\\" +
                                 findData.cFileName + L"\\nvngx.dll";
        if (!FileExists(candidate)) continue;

        HMODULE module = LoadLibraryW(candidate.c_str());
        if (module && GetProcAddress(module, "NVSDK_NGX_D3D12_GetCapabilityParameters")) {
            found = module;
            break;
        }
        if (module) FreeLibrary(module);
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return found;
}

} // namespace

bool D3D12NrExecutor::Load() {
    const std::wstring exeDir = ExeDirectory();

    for (const wchar_t* candidate : kDriverCandidates) {
        HMODULE module = GetModuleHandleW(candidate);
        if (!module) module = LoadLibraryW(candidate);
        if (!module) continue;
        if (!Symbol<GetCapFn>(module, "NVSDK_NGX_D3D12_GetCapabilityParameters")) continue;
        driverModule_ = module;
        break;
    }
    if (!driverModule_) {
        driverModule_ = LoadDriverNgxFromDriverStore();
    }
    if (!driverModule_) {
        status_ = "no module answers NVSDK_NGX_D3D12_GetCapabilityParameters (checked beside the "
                  "executable and the driver store)";
        return false;
    }
    driverInit_ = Symbol<InitFn>(driverModule_, "NVSDK_NGX_D3D12_Init");
    getCapabilityParams_ = Symbol<GetCapFn>(driverModule_, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    if (!driverInit_ || !getCapabilityParams_) {
        status_ = "driver module missing Init or GetCapabilityParameters";
        return false;
    }

    const std::wstring forwarderPath = exeDir + L"nvngx.dll_dlssnr.dll";
    if (!FileExists(forwarderPath)) {
        status_ = "nvngx.dll_dlssnr.dll not found beside NRFusionHost64.exe";
        return false;
    }
    forwarderModule_ = LoadLibraryW(forwarderPath.c_str());
    if (!forwarderModule_) {
        status_ = "nvngx.dll_dlssnr.dll would not load";
        return false;
    }
    create_ = Symbol<CreateFn>(forwarderModule_, "dlssnr_call_create");
    evaluate_ = Symbol<EvaluateFn>(forwarderModule_, "dlssnr_call_evaluate_v2");
    release_ = Symbol<ReleaseFn>(forwarderModule_, "dlssnr_call_release");
    setFloatSlot_ = Symbol<SetFloatSlotFn>(forwarderModule_, "dlssnr_call_set_float_slot");
    probeFloat_ = Symbol<ProbeFloatFn>(forwarderModule_, "dlssnr_call_probe_float");
    if (!create_ || !evaluate_ || !release_) {
        status_ = "nvngx.dll_dlssnr.dll missing the NR v2 exports";
        return false;
    }

    const std::wstring snippetPath = exeDir + L"nvngx_dlssnr.dll";
    if (!FileExists(snippetPath)) {
        status_ = "nvngx_dlssnr.dll (the model itself) not found beside NRFusionHost64.exe";
        return false;
    }
    snippetPath_ = snippetPath;

    status_ = "loaded";
    return true;
}

void D3D12NrExecutor::DiscoverFloatSlot() {
    if (floatSlotKnown_ || !capabilityParams_ || !probeFloat_ || !setFloatSlot_) return;
    floatSlotKnown_ = true;

    static const char* kProbeKey = "DLSSNR.OptiScalerFloatProbe";
    static const int kCandidates[] = { 1, 2, 5, 6, 7, 4, 3, 0 };
    const float expected = 0.375f;

    for (int slot : kCandidates) {
        float readBack = 0.0f;
        probeFloat_(capabilityParams_, kProbeKey, expected, slot);
        if (capabilityParams_->Get(kProbeKey, &readBack) == kNgxSuccess && readBack == expected) {
            setFloatSlot_(slot);
            return;
        }
    }
    status_ = "float parameter slot not found; intensity/structure/tone/skin knobs will have no effect";
}

bool D3D12NrExecutor::Init(ID3D12Device* device) {
    if (!driverInit_ || !getCapabilityParams_) return false;
    if (capabilityParams_) return true;

    if (driverInit_(0x24480451ull, L"", device, nullptr, 0x15u) != kNgxSuccess) {
        status_ = "NVSDK_NGX_D3D12_Init refused";
        return false;
    }
    if (getCapabilityParams_(&capabilityParams_) != kNgxSuccess || !capabilityParams_) {
        capabilityParams_ = nullptr;
        status_ = "GetCapabilityParameters refused";
        return false;
    }
    DiscoverFloatSlot();
    status_ = "initialised";
    return true;
}

} // namespace nrfusion
