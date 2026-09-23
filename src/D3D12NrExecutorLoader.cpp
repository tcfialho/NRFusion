#include "nrfusion/D3D12NrExecutor.hpp"

#include <array>

namespace nrfusion {
namespace {

constexpr std::array<const wchar_t*, 3> kDriverCandidates = { L"nvngx.dll", L"_nvngx.dll", L"nvngx_dlss.dll" };

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
    if (driverModule_ != nullptr && forwarderModule_ != nullptr &&
        driverInit_ != nullptr && getCapabilityParams_ != nullptr &&
        create_ != nullptr && evaluate_ != nullptr && release_ != nullptr &&
        !snippetPath_.empty()) {
        status_ = "loaded";
        return true;
    }
    if (driverModule_ != nullptr || forwarderModule_ != nullptr) {
        status_ = "partial NR loader state; call Shutdown before retrying Load";
        return false;
    }

    const std::wstring exeDir = ExeDirectory();
    const std::wstring forwarderPath = exeDir + L"nvngx.dll_dlssnr.dll";
    const std::wstring snippetPath = exeDir + L"nvngx_dlssnr.dll";
    if (!FileExists(forwarderPath) || !FileExists(snippetPath)) {
        status_ = !FileExists(forwarderPath)
            ? "nvngx.dll_dlssnr.dll not found beside NRFusionHost64.exe"
            : "nvngx_dlssnr.dll (the model itself) not found beside NRFusionHost64.exe";
        return false;
    }

    HMODULE driver = nullptr;
    bool driverOwned = false;

    for (const wchar_t* candidate : kDriverCandidates) {
        HMODULE module = GetModuleHandleW(candidate);
        bool owned = false;
        if (module == nullptr) {
            module = LoadLibraryW(candidate);
            owned = module != nullptr;
        }
        if (module == nullptr) continue;
        if (!Symbol<GetCapFn>(module, "NVSDK_NGX_D3D12_GetCapabilityParameters")) {
            if (owned) FreeLibrary(module);
            continue;
        }
        driver = module;
        driverOwned = owned;
        break;
    }
    if (driver == nullptr) {
        driver = LoadDriverNgxFromDriverStore();
        driverOwned = driver != nullptr;
    }
    if (driver == nullptr) {
        status_ = "no module answers NVSDK_NGX_D3D12_GetCapabilityParameters";
        return false;
    }

    const InitFn driverInit = Symbol<InitFn>(driver, "NVSDK_NGX_D3D12_Init");
    const GetCapFn getCapabilityParams =
        Symbol<GetCapFn>(driver, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    if (driverInit == nullptr || getCapabilityParams == nullptr) {
        if (driverOwned) FreeLibrary(driver);
        status_ = "driver module missing Init or GetCapabilityParameters";
        return false;
    }

    HMODULE forwarder = LoadLibraryW(forwarderPath.c_str());
    if (forwarder == nullptr) {
        if (driverOwned) FreeLibrary(driver);
        status_ = "nvngx.dll_dlssnr.dll would not load";
        return false;
    }

    const CreateFn create = Symbol<CreateFn>(forwarder, "dlssnr_call_create");
    const EvaluateFn evaluate = Symbol<EvaluateFn>(forwarder, "dlssnr_call_evaluate_v2");
    const ReleaseFn release = Symbol<ReleaseFn>(forwarder, "dlssnr_call_release");
    const SetFloatSlotFn setFloatSlot =
        Symbol<SetFloatSlotFn>(forwarder, "dlssnr_call_set_float_slot");
    const ProbeFloatFn probeFloat =
        Symbol<ProbeFloatFn>(forwarder, "dlssnr_call_probe_float");
    if (create == nullptr || evaluate == nullptr || release == nullptr) {
        FreeLibrary(forwarder);
        if (driverOwned) FreeLibrary(driver);
        status_ = "nvngx.dll_dlssnr.dll missing the NR v2 exports";
        return false;
    }

    driverModule_ = driver;
    driverModuleOwned_ = driverOwned;
    forwarderModule_ = forwarder;
    driverInit_ = driverInit;
    getCapabilityParams_ = getCapabilityParams;
    create_ = create;
    evaluate_ = evaluate;
    release_ = release;
    setFloatSlot_ = setFloatSlot;
    probeFloat_ = probeFloat;
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
    if (device == nullptr || !driverInit_ || !getCapabilityParams_) {
        status_ = "invalid NR init request";
        return false;
    }
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
