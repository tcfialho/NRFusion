#include "streamline_mfg.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <utility>

#if defined(NRFUSION_REQUIEM_STREAMLINE)
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <tlhelp32.h>
#endif

namespace requiem {

struct StreamlineMfg::Impl {
    bool requested = false;
    bool required = false;
    bool initialized = false;
    bool ready = false;
    std::uint32_t multiplier = 0;
    std::uint64_t appPresents = 0;
    std::uint64_t actualPresents = 0;
    std::uint32_t reportedMax = 0;
    std::string status = "disabled";
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    HMODULE interposer = nullptr;
    HMODULE dlssgModule = nullptr;
    std::wstring pluginPath;
    sl::ViewportHandle viewport{0};
    sl::FrameToken* frame = nullptr;
    sl::DLSSGOptions options{};
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::uint32_t backBuffers = 0;
    std::uint32_t currentFrame = 0;
    std::uint32_t lastDlssgStatus = 0;

    PFun_slInit* init = nullptr;
    PFun_slShutdown* shutdown = nullptr;
    PFun_slIsFeatureSupported* isFeatureSupported = nullptr;
    PFun_slSetD3DDevice* setD3DDevice = nullptr;
    PFun_slUpgradeInterface* upgradeInterface = nullptr;
    PFun_slGetFeatureFunction* getFeatureFunction = nullptr;
    PFun_slGetNewFrameToken* getNewFrameToken = nullptr;
    PFun_slSetConstants* setConstants = nullptr;
    PFun_slSetTagForFrame* setTagForFrame = nullptr;
    PFun_slDLSSGSetOptions* setDlssgOptions = nullptr;
    PFun_slDLSSGGetState* getDlssgState = nullptr;
    PFun_slReflexSetOptions* setReflexOptions = nullptr;
    PFun_slReflexSleep* reflexSleep = nullptr;
    PFun_slReflexGetState* getReflexState = nullptr;
    PFun_slPCLSetMarker* pclSetMarker = nullptr;
    int(__cdecl* patchMfgModule)(HMODULE) = nullptr;
    void(__cdecl* followGameControl)() = nullptr;
    int(__cdecl* processSetOptions)(unsigned int*, unsigned int*) = nullptr;
    int(__cdecl* processGetState)(unsigned int*) = nullptr;
    void(__cdecl* notifyFrameBoundary)() = nullptr;
#endif
};

namespace {

#if defined(NRFUSION_REQUIEM_STREAMLINE)
template <typename T>
T Symbol(HMODULE module, const char* name) {
    return reinterpret_cast<T>(
        reinterpret_cast<void*>(GetProcAddress(module, name)));
}

std::wstring ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD count = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (count == 0 || count >= MAX_PATH) return {};
    return std::filesystem::path(path).parent_path().wstring();
}

void OnDlssgApiError(const sl::APIError& error) {
    std::fprintf(stderr, "[MFG] underlying API error=0x%08lX\n",
                 static_cast<unsigned long>(error.hres));
}

std::string ResultText(const char* action, sl::Result result) {
    std::ostringstream out;
    out << action << " failed: " << static_cast<int>(result);
    return out.str();
}
void Identity(sl::float4x4& m) {
    m.setRow(0, sl::float4(1, 0, 0, 0));
    m.setRow(1, sl::float4(0, 1, 0, 0));
    m.setRow(2, sl::float4(0, 0, 1, 0));
    m.setRow(3, sl::float4(0, 0, 0, 1));
}

template <typename ImplT>
bool GetFeatureFunction(
    ImplT& impl, sl::Feature feature,
    const char* name, void** output) {
    void* function = nullptr;
    const auto result =
        impl.getFeatureFunction(feature, name, function);
    if (result != sl::Result::eOk || !function) {
        impl.status = ResultText(name, result);
        return false;
    }
    *output = function;
    return true;
}

template <typename ImplT>
bool SetMarker(ImplT& impl, sl::PCLMarker marker) {
    if (!impl.ready || !impl.frame || !impl.pclSetMarker) return false;
    const auto result = impl.pclSetMarker(marker, *impl.frame);
    if (result != sl::Result::eOk) {
        impl.status = ResultText("slPCLSetMarker", result);
        return false;
    }
    return true;
}

static HWND g_mfgWindow = nullptr;
static HWND(WINAPI* g_realGetForegroundWindow)() = nullptr;

static HWND WINAPI SlHookGetForegroundWindow() {
    HWND fg = g_realGetForegroundWindow ? g_realGetForegroundWindow() : GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fgPid);
    if (fgPid == GetCurrentProcessId()) return fg;
    return g_mfgWindow ? g_mfgWindow : fg;
}

static bool HookModuleImport(HMODULE module, const char* targetDll, const char* targetFunc, void* newFunc, void** origFunc) {
    if (!module) return false;
    auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
        reinterpret_cast<BYTE*>(module) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

    auto& importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0 || importDir.Size == 0) return false;

    auto* importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
        reinterpret_cast<BYTE*>(module) + importDir.VirtualAddress);

    for (; importDesc->Name != 0; ++importDesc) {
        const char* modName = reinterpret_cast<const char*>(
            reinterpret_cast<BYTE*>(module) + importDesc->Name);
        if (_stricmp(modName, targetDll) == 0) {
            auto* thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(
                reinterpret_cast<BYTE*>(module) + importDesc->FirstThunk);
            auto* origThunk = importDesc->OriginalFirstThunk
                ? reinterpret_cast<PIMAGE_THUNK_DATA>(
                      reinterpret_cast<BYTE*>(module) + importDesc->OriginalFirstThunk)
                : thunk;

            for (; thunk->u1.Function != 0; ++thunk, ++origThunk) {
                if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
                auto* importByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(
                    reinterpret_cast<BYTE*>(module) + origThunk->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char*>(importByName->Name), targetFunc) == 0) {
                    if (thunk->u1.Function == reinterpret_cast<uintptr_t>(newFunc)) return true;
                    DWORD oldProtect = 0;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                        if (origFunc && !*origFunc) {
                            *origFunc = reinterpret_cast<void*>(thunk->u1.Function);
                        }
                        thunk->u1.Function = reinterpret_cast<uintptr_t>(newFunc);
                        VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static void ApplyFocusHooks() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            HookModuleImport(me.hModule, "USER32.dll", "GetForegroundWindow",
                             reinterpret_cast<void*>(&SlHookGetForegroundWindow),
                             reinterpret_cast<void**>(&g_realGetForegroundWindow));
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}
#endif

} // namespace

void StreamlineMfg::SetWindow(void* hwnd) {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    g_mfgWindow = static_cast<HWND>(hwnd);
    ApplyFocusHooks();
#else
    (void)hwnd;
#endif
}

StreamlineMfg::StreamlineMfg() : impl_(std::make_unique<Impl>()) {}
StreamlineMfg::~StreamlineMfg() = default;
#include "streamline_mfg_init.inc"
#include "streamline_mfg_configure.inc"
#include "streamline_mfg_frame.inc"
bool StreamlineMfg::Requested() const noexcept {
    return impl_->requested;
}
bool StreamlineMfg::Ready() const noexcept {
    return impl_->ready;
}
std::uint64_t StreamlineMfg::AppPresents() const noexcept {
    return impl_->appPresents;
}
std::uint64_t StreamlineMfg::ActualPresents() const noexcept {
    return impl_->actualPresents;
}
std::uint32_t StreamlineMfg::ReportedMaxGenerated() const noexcept {
    return impl_->reportedMax;
}
std::uint32_t StreamlineMfg::DlssgStatus() const noexcept {
    return impl_->lastDlssgStatus;
}
const std::string& StreamlineMfg::Status() const noexcept {
    return impl_->status;
}

bool StreamlineMfg::GatePassed() const noexcept {
    if (!impl_->requested || !impl_->ready ||
        impl_->appPresents == 0 || impl_->lastDlssgStatus != 0)
        return false;
    const double ratio =
        static_cast<double>(impl_->actualPresents) /
        static_cast<double>(impl_->appPresents);
    return impl_->actualPresents > impl_->appPresents &&
           ratio >= static_cast<double>(impl_->multiplier) - 0.5;
}

} // namespace requiem
