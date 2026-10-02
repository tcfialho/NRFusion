#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "RuntimeOverlayCursor.hpp"
#include "nrfusion/Logger.hpp"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace nrfusion {
namespace {

using SetCursorPosition = BOOL(WINAPI*)(int, int);
using SetCursorClip = BOOL(WINAPI*)(const RECT*);
using GetCursorClip = BOOL(WINAPI*)(RECT*);
using SetCursorShape = HCURSOR(WINAPI*)(HCURSOR);
SetCursorPosition originalSetCursorPos = nullptr;
SetCursorClip originalClipCursor = nullptr;
GetCursorClip originalGetClipCursor = nullptr;
SetCursorShape originalSetCursor = nullptr;
std::atomic<bool> blocking{false};
std::atomic<UINT64> blockedWarps{0};
std::atomic<UINT64> blockedClips{0};
std::mutex clipMutex;
RECT savedClip{};
RECT deferredClip{};
bool savedClipValid = false;
bool hasDeferredClip = false;
bool deferredClipIsNull = false;

BOOL WINAPI BlockGameCursorWarp(int x, int y) {
    if (blocking.load(std::memory_order_acquire)) {
        blockedWarps.fetch_add(1, std::memory_order_relaxed);
        return TRUE;
    }
    return originalSetCursorPos(x, y);
}

BOOL WINAPI DeferGameCursorClip(const RECT* clip) {
    std::lock_guard lock(clipMutex);
    if (!blocking.load(std::memory_order_acquire)) return originalClipCursor(clip);
    hasDeferredClip = true;
    deferredClipIsNull = clip == nullptr;
    if (clip) deferredClip = *clip;
    blockedClips.fetch_add(1, std::memory_order_relaxed);
    return TRUE;
}

BOOL WINAPI ReadGameCursorClip(RECT* clip) {
    if (!clip) return FALSE;
    std::lock_guard lock(clipMutex);
    if (blocking.load(std::memory_order_acquire) && savedClipValid) {
        *clip = hasDeferredClip && !deferredClipIsNull ? deferredClip : savedClip;
        return TRUE;
    }
    return originalGetClipCursor(clip);
}

HCURSOR WINAPI HideSystemCursorForMenu(HCURSOR cursor) {
    return originalSetCursor(blocking.load(std::memory_order_acquire) ? nullptr : cursor);
}

bool InstallCursorHook(const wchar_t* module, const char* function, void* replacement,
                       void** original, void** target) {
    const auto status = MH_CreateHookApiEx(module, function, replacement, original, target);
    if (status == MH_OK) return true;
    NRF_LOG_ERROR("OverlayCursor", "Hook %s failed: %s", function, MH_StatusToString(status));
    return false;
}

} // namespace

bool InitializeOverlayCursorControl() {
    static std::once_flag initialized;
    static bool ready = false;
    std::call_once(initialized, [] {
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return;
        void* targets[4]{};
        const bool created = InstallCursorHook(L"user32.dll", "SetCursorPos",
            reinterpret_cast<void*>(&BlockGameCursorWarp), reinterpret_cast<void**>(&originalSetCursorPos), &targets[0]) &&
            InstallCursorHook(L"user32.dll", "ClipCursor", reinterpret_cast<void*>(&DeferGameCursorClip),
                reinterpret_cast<void**>(&originalClipCursor), &targets[1]) &&
            InstallCursorHook(L"user32.dll", "GetClipCursor", reinterpret_cast<void*>(&ReadGameCursorClip),
                reinterpret_cast<void**>(&originalGetClipCursor), &targets[2]) &&
            InstallCursorHook(L"user32.dll", "SetCursor", reinterpret_cast<void*>(&HideSystemCursorForMenu),
                reinterpret_cast<void**>(&originalSetCursor), &targets[3]);
        if (created) {
            for (void* target : targets) MH_QueueEnableHook(target);
            ready = MH_ApplyQueued() == MH_OK;
        }
        if (!ready) {
            for (void* target : targets) {
                if (!target) continue;
                MH_DisableHook(target);
                MH_RemoveHook(target);
            }
        }
        NRF_LOG_INFO("OverlayCursor", "Cursor interception ready=%d (position/clip/shape)", ready);
    });
    return ready;
}

void BeginOverlayCursorControl() {
    if (!InitializeOverlayCursorControl()) return;
    std::lock_guard lock(clipMutex);
    if (blocking.load(std::memory_order_acquire)) return;
    savedClipValid = originalGetClipCursor(&savedClip) != FALSE;
    hasDeferredClip = deferredClipIsNull = false;
    blockedWarps = blockedClips = 0;
    blocking.store(true, std::memory_order_release);
    originalClipCursor(nullptr);
    originalSetCursor(nullptr);
}

void EndOverlayCursorControl() {
    std::lock_guard lock(clipMutex);
    if (!blocking.exchange(false, std::memory_order_acq_rel)) return;
    const RECT* restored = savedClipValid ? &savedClip : nullptr;
    if (hasDeferredClip) restored = deferredClipIsNull ? nullptr : &deferredClip;
    originalClipCursor(restored);
    NRF_LOG_INFO("OverlayCursor", "Restored game cursor; blocked warps=%llu clips=%llu",
                 blockedWarps.load(), blockedClips.load());
}

} // namespace nrfusion
