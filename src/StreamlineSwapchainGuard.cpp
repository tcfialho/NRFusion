#include "nrfusion/StreamlineSwapchainGuard.hpp"
#include "nrfusion/Logger.hpp"
#include <MinHook.h>
#include <sl_core_types.h>
#include <cstring>
#include <cwctype>
#include <vector>

namespace nrfusion {
namespace {

using CommonGetBufferFn = HRESULT(*)(void*, IDXGISwapChain*, uint32_t, void**);
static CommonGetBufferFn s_origCommonGetBuffer[4] = {};
static void* s_hookedCommonGetBufferTargets[4] = {};
static size_t s_hookedCommonGetBufferCount = 0;

#define DEFINE_COMMON_GET_BUFFER_STUB(Slot) \
HRESULT HookedCommonGetBuffer##Slot(void* s, IDXGISwapChain* c, uint32_t i, void** r) { \
    if (!c || !r) return DXGI_ERROR_INVALID_CALL; \
    __try { return s_origCommonGetBuffer[Slot](s, c, i, r); } \
    __except (EXCEPTION_EXECUTE_HANDLER) { return DXGI_ERROR_INVALID_CALL; } \
}
DEFINE_COMMON_GET_BUFFER_STUB(0)
DEFINE_COMMON_GET_BUFFER_STUB(1)
DEFINE_COMMON_GET_BUFFER_STUB(2)
DEFINE_COMMON_GET_BUFFER_STUB(3)
#undef DEFINE_COMMON_GET_BUFFER_STUB

static const CommonGetBufferFn s_hookStubs[4] = {
    &HookedCommonGetBuffer0,
    &HookedCommonGetBuffer1,
    &HookedCommonGetBuffer2,
    &HookedCommonGetBuffer3,
};

using SlHookCreateSwapChainForHwndFn = sl::Result(*)(
    void* factory, void* device, HWND hwnd, const void* desc,
    const void* fullscreenDesc, void* restrictToOutput,
    IDXGISwapChain1** swapChain, bool* handled);

using SlHookCreateSwapChainFn = sl::Result(*)(
    void* factory, void* device, const void* desc,
    IDXGISwapChain** swapChain, bool* handled);

using SlHookCreateSwapChainForCoreWindowFn = sl::Result(*)(
    void* factory, void* device, IUnknown* window, const void* desc,
    void* restrictToOutput, IDXGISwapChain1** swapChain, bool* handled);

static SlHookCreateSwapChainForHwndFn s_origSlCreateForHwnd = nullptr;
static SlHookCreateSwapChainFn s_origSlCreate = nullptr;
static SlHookCreateSwapChainForCoreWindowFn s_origSlCreateCore = nullptr;
static void* s_hookedDlssgTargets[3] = {};

sl::Result STDMETHODCALLTYPE HookedSlCreateSwapChainForHwnd(
    void* factory, void* device, HWND hwnd, const void* desc,
    const void* fullscreenDesc, void* restrictToOutput,
    IDXGISwapChain1** swapChain, bool* handled) {
    if (!swapChain || !*swapChain) {
        if (handled) *handled = false;
        return sl::Result::eOk;
    }
    __try {
        return s_origSlCreateForHwnd(
            factory, device, hwnd, desc, fullscreenDesc, restrictToOutput, swapChain, handled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (handled) *handled = false;
        return sl::Result::eErrorNotInitialized;
    }
}

sl::Result STDMETHODCALLTYPE HookedSlCreateSwapChain(
    void* factory, void* device, const void* desc,
    IDXGISwapChain** swapChain, bool* handled) {
    if (!swapChain || !*swapChain) {
        if (handled) *handled = false;
        return sl::Result::eOk;
    }
    __try {
        return s_origSlCreate(factory, device, desc, swapChain, handled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (handled) *handled = false;
        return sl::Result::eErrorNotInitialized;
    }
}

sl::Result STDMETHODCALLTYPE HookedSlCreateSwapChainForCoreWindow(
    void* factory, void* device, IUnknown* window, const void* desc,
    void* restrictToOutput, IDXGISwapChain1** swapChain, bool* handled) {
    if (!swapChain || !*swapChain) {
        if (handled) *handled = false;
        return sl::Result::eOk;
    }
    __try {
        return s_origSlCreateCore(
            factory, device, window, desc, restrictToOutput, swapChain, handled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (handled) *handled = false;
        return sl::Result::eErrorNotInitialized;
    }
}

} // namespace

StreamlineSwapchainGuard& StreamlineSwapchainGuard::Instance() noexcept {
    static StreamlineSwapchainGuard instance;
    return instance;
}

bool StreamlineSwapchainGuard::Install(HMODULE targetModule) noexcept {
    const auto mhInit = MH_Initialize();
    if (mhInit != MH_OK && mhInit != MH_ERROR_ALREADY_INITIALIZED) return false;

    if (targetModule) {
        wchar_t path[MAX_PATH] = {};
        bool matched = false;
        if (GetModuleFileNameW(targetModule, path, MAX_PATH) > 0) {
            for (wchar_t* p = path; *p; ++p) *p = static_cast<wchar_t>(towlower(*p));
            if (wcsstr(path, L"sl.common")) {
                InstallCommonGuards(targetModule);
                matched = true;
            }
            if (wcsstr(path, L"sl.dlss_g")) {
                InstallDlssgGuards(targetModule);
                matched = true;
            }
        }
        if (!matched) {
            InstallCommonGuards(targetModule);
            InstallDlssgGuards(targetModule);
        }
    }

    if (!commonGuarded_.load()) {
        HMODULE common = GetModuleHandleW(L"sl.common.dll");
        if (common) InstallCommonGuards(common);
    }

    if (!dlssgGuarded_.load()) {
        HMODULE dlssg = GetModuleHandleW(L"sl.dlss_g.dll");
        if (dlssg) InstallDlssgGuards(dlssg);
    }

    return commonGuarded_.load() || dlssgGuarded_.load();
}

bool StreamlineSwapchainGuard::InstallCommonGuards(HMODULE commonModule) noexcept {
    if (!commonModule) return false;
    bool expected = false;
    if (!commonGuarded_.compare_exchange_strong(expected, true)) return true;

    auto* base = reinterpret_cast<std::byte*>(commonModule);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        commonGuarded_.store(false);
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        commonGuarded_.store(false);
        return false;
    }

    auto* sections = IMAGE_FIRST_SECTION(nt);
    static const uint8_t kSignature[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x02
    };

    size_t hooked = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& sec = sections[i];
        if ((sec.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const std::byte* start = base + sec.VirtualAddress;
        const size_t size = sec.Misc.VirtualSize;
        if (size < sizeof(kSignature)) continue;

        for (size_t offset = 0; offset <= size - sizeof(kSignature); ++offset) {
            if (std::memcmp(start + offset, kSignature, sizeof(kSignature)) == 0) {
                if (s_hookedCommonGetBufferCount >= 4) break;
                void* target = const_cast<void*>(reinterpret_cast<const void*>(start + offset));
                const size_t slot = s_hookedCommonGetBufferCount;
                if (MH_CreateHook(target, reinterpret_cast<void*>(s_hookStubs[slot]),
                                  reinterpret_cast<void**>(&s_origCommonGetBuffer[slot])) == MH_OK) {
                    if (MH_EnableHook(target) == MH_OK) {
                        s_hookedCommonGetBufferTargets[slot] = target;
                        s_hookedCommonGetBufferCount++;
                        hooked++;
                    }
                }
            }
        }
    }
    if (hooked > 0) {
        NRF_LOG_INFO("StreamlineGuard", "Protected %zu sl.common GetBuffer callsite(s)", hooked);
    } else {
        commonGuarded_.store(false);
    }
    return hooked > 0;
}

bool StreamlineSwapchainGuard::InstallDlssgGuards(HMODULE dlssgModule) noexcept {
    if (!dlssgModule) return false;
    bool expected = false;
    if (!dlssgGuarded_.compare_exchange_strong(expected, true)) return true;

    using GetPluginFn = void*(*)(const char*);
    auto getFn = reinterpret_cast<GetPluginFn>(GetProcAddress(dlssgModule, "slGetPluginFunction"));
    if (!getFn) {
        dlssgGuarded_.store(false);
        return false;
    }

    size_t hooked = 0;
    void* forHwnd = getFn("slHookCreateSwapChainForHwnd");
    if (forHwnd && MH_CreateHook(forHwnd, reinterpret_cast<void*>(&HookedSlCreateSwapChainForHwnd),
                                 reinterpret_cast<void**>(&s_origSlCreateForHwnd)) == MH_OK) {
        if (MH_EnableHook(forHwnd) == MH_OK) { s_hookedDlssgTargets[0] = forHwnd; hooked++; }
    }

    void* legacy = getFn("slHookCreateSwapChain");
    if (legacy && MH_CreateHook(legacy, reinterpret_cast<void*>(&HookedSlCreateSwapChain),
                                 reinterpret_cast<void**>(&s_origSlCreate)) == MH_OK) {
        if (MH_EnableHook(legacy) == MH_OK) { s_hookedDlssgTargets[1] = legacy; hooked++; }
    }

    void* core = getFn("slHookCreateSwapChainForCoreWindow");
    if (core && MH_CreateHook(core, reinterpret_cast<void*>(&HookedSlCreateSwapChainForCoreWindow),
                               reinterpret_cast<void**>(&s_origSlCreateCore)) == MH_OK) {
        if (MH_EnableHook(core) == MH_OK) { s_hookedDlssgTargets[2] = core; hooked++; }
    }

    if (hooked > 0) {
        NRF_LOG_INFO("StreamlineGuard", "Protected %zu sl.dlss_g swapchain hook(s)", hooked);
    } else {
        dlssgGuarded_.store(false);
    }
    return hooked > 0;
}

void StreamlineSwapchainGuard::OnPostCreateSwapChainForHwnd(
    IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
    IDXGIOutput* restrictToOutput, IDXGISwapChain1** swapChain) noexcept {
    (void)factory; (void)device; (void)hwnd; (void)desc; (void)fullscreenDesc; (void)restrictToOutput; (void)swapChain;
}

void StreamlineSwapchainGuard::OnPostCreateSwapChain(
    IDXGIFactory* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** swapChain) noexcept {
    (void)factory; (void)device; (void)desc; (void)swapChain;
}

void StreamlineSwapchainGuard::Reset() noexcept {
    for (size_t i = 0; i < s_hookedCommonGetBufferCount; ++i) {
        if (s_hookedCommonGetBufferTargets[i]) {
            MH_DisableHook(s_hookedCommonGetBufferTargets[i]);
            MH_RemoveHook(s_hookedCommonGetBufferTargets[i]);
        }
    }
    s_hookedCommonGetBufferCount = 0;
    for (void*& target : s_hookedDlssgTargets) {
        if (target) { MH_DisableHook(target); MH_RemoveHook(target); target = nullptr; }
    }
    s_origSlCreateForHwnd = nullptr;
    s_origSlCreate = nullptr;
    s_origSlCreateCore = nullptr;
    commonGuarded_.store(false);
    dlssgGuarded_.store(false);
}

} // namespace nrfusion
