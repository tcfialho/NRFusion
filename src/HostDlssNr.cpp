#include "nrfusion/HostDlssNr.hpp"

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

// A game process finds the driver's nvngx.dll already loaded (something in its own module list --
// the platform's own loader, or a DLSS proxy -- pulled it in first). A bare standalone process, like
// this host, has nothing to do that, and the driver store is deliberately off the standard DLL
// search path, so a plain LoadLibraryW(L"nvngx.dll") finds nothing here even though the file is on
// disk. Enumerating the driver store and loading it by full path is the fallback for that case.
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

bool HostDlssNr::Load() {
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

void HostDlssNr::DiscoverFloatSlot() {
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

bool HostDlssNr::Init(ID3D12Device* device) {
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

bool HostDlssNr::EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                               const DlssNrTuning& tuning) {
    justBuilt_ = false;
    if (!capabilityParams_ || !create_) return false;
    if (feature_ && featureWidth_ == width && featureHeight_ == height) return true;
    if (feature_ && release_) {
        release_(feature_);
        feature_ = nullptr;
    }

    // The device is recovered from the command list's own allocator implicitly by the forwarder's
    // cached snippet; passing nullptr here would only matter on the very first device the snippet
    // sees, which Init() above already primed.
    ID3D12Device* device = nullptr;
    cmdList->GetDevice(IID_PPV_ARGS(&device));

    feature_ = create_(snippetPath_.c_str(), L"", device, cmdList, capabilityParams_, width, height,
                       tuning.preset, tuning.intensity, tuning.style, tuning.localStructure,
                       tuning.localTone, tuning.skinStructure, tuning.autoMask ? 1 : 0,
                       tuning.uiCorrection);
    if (device) device->Release();

    if (!feature_) {
        status_ = "dlssnr_call_create failed";
        return false;
    }
    featureWidth_ = width;
    featureHeight_ = height;
    justBuilt_ = true;
    status_ = "feature built; usable starting next call";
    return true;
}

bool HostDlssNr::Evaluate(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
                          ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
                          bool depthInverted, bool reset, const DlssNrTuning& tuning, uint32_t guideWidth,
                          uint32_t guideHeight, uint32_t motionWidth, uint32_t motionHeight) {
    if (!feature_ || !evaluate_ || !capabilityParams_) return false;
    if (guideWidth == 0) guideWidth = width;
    if (guideHeight == 0) guideHeight = height;
    if (motionWidth == 0) motionWidth = width;
    if (motionHeight == 0) motionHeight = height;

    const int result = evaluate_(cmdList, feature_, capabilityParams_, color, depth, motion, output,
                                 width, height, guideWidth, guideHeight, motionWidth, motionHeight,
                                 0, 0, 0, 0, depthInverted ? 1 : 0, reset ? 1 : 0, tuning.intensity,
                                 tuning.style, tuning.localStructure, tuning.localTone,
                                 tuning.skinStructure, tuning.autoMask ? 1 : 0,
                                 static_cast<float>(motionWidth), static_cast<float>(motionHeight));
    status_ = result == kNgxSuccess ? "evaluated" : "dlssnr_call_evaluate_v2 failed";
    return result == kNgxSuccess;
}

void HostDlssNr::Shutdown() {
    if (feature_ && release_) release_(feature_);
    feature_ = nullptr;
    featureWidth_ = featureHeight_ = 0;
    capabilityParams_ = nullptr;
    if (forwarderModule_) { FreeLibrary(forwarderModule_); forwarderModule_ = nullptr; }
    driverModule_ = nullptr;
    status_ = "shut down";
}

} // namespace nrfusion
