#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/MfgModuleWatcher.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include "nrfusion/Logger.hpp"

#include <cwchar>

namespace nrfusion {
namespace {

struct UnicodeString {
    USHORT length;
    USHORT maximumLength;
    PWSTR buffer;
};

struct LoadedNotificationData {
    ULONG flags;
    const UnicodeString* fullDllName;
    const UnicodeString* baseDllName;
    PVOID dllBase;
    ULONG sizeOfImage;
};struct UnloadedNotificationData {
    ULONG flags;
    const UnicodeString* fullDllName;
    const UnicodeString* baseDllName;
    PVOID dllBase;
    ULONG sizeOfImage;
};

union NotificationData {
    LoadedNotificationData loaded;
    UnloadedNotificationData unloaded;
};

using NotificationFn =
    void(CALLBACK*)(ULONG, const NotificationData*, void*);
using RegisterFn =
    LONG(NTAPI*)(ULONG, NotificationFn, void*, void**);
using UnregisterFn = LONG(NTAPI*)(void*);

bool EqualsInsensitive(
    const UnicodeString* value, const wchar_t* expected) noexcept {
    if (!value || !value->buffer || !expected) return false;
    const size_t count = value->length / sizeof(wchar_t);
    const size_t expectedCount = std::wcslen(expected);
    if (count != expectedCount) return false;
    for (size_t i = 0; i < count; ++i) {
        wchar_t a = value->buffer[i];
        wchar_t b = expected[i];
        if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
        if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
        if (a != b) return false;
    }
    return true;
}

bool ContainsInsensitive(
    const UnicodeString* value, const wchar_t* needle) noexcept {
    if (!value || !value->buffer || !needle) return false;
    const size_t count = value->length / sizeof(wchar_t);
    const size_t needleCount = std::wcslen(needle);
    if (count < needleCount) return false;
    for (size_t i = 0; i <= count - needleCount; ++i) {
        bool match = true;
        for (size_t j = 0; j < needleCount; ++j) {
            wchar_t a = value->buffer[i + j];
            wchar_t b = needle[j];
            if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
            if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

RegisterFn ResolveRegister() noexcept {
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? reinterpret_cast<RegisterFn>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification")) : nullptr;
}

UnregisterFn ResolveUnregister() noexcept {
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll ? reinterpret_cast<UnregisterFn>(
        GetProcAddress(ntdll, "LdrUnregisterDllNotification")) : nullptr;
}

} // namespace

MfgModuleWatcher& MfgModuleWatcher::Instance() {
    static MfgModuleWatcher instance;
    return instance;
}

bool MfgModuleWatcher::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return true;

    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const auto registerFn = ResolveRegister();
    if (!wakeEvent_ || !registerFn) {
        if (wakeEvent_) CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
        running_.store(false);
        return false;
    }

    const LONG status = registerFn(
        0, reinterpret_cast<NotificationFn>(&MfgModuleWatcher::Notification),
        this, &notificationCookie_);
    if (status < 0) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
        notificationCookie_ = nullptr;
        running_.store(false);
        return false;
    }

    worker_ = std::thread(&MfgModuleWatcher::ThreadProc, this);
    ScanAndPatchLoadedModules();
    return true;
}

void MfgModuleWatcher::Stop(bool isProcessTerminating) {
    if (!running_.exchange(false)) return;

    if (notificationCookie_ && !isProcessTerminating) {
        if (const auto unregisterFn = ResolveUnregister())
            unregisterFn(notificationCookie_);
        notificationCookie_ = nullptr;
    }

    if (wakeEvent_) SetEvent(wakeEvent_);
    if (worker_.joinable()) {
        if (isProcessTerminating) worker_.detach();
        else worker_.join();
    }
    HMODULE leftover = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (leftover) {
        FreeLibrary(leftover);
    }
    if (!isProcessTerminating && wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
}

using EnumProcessModulesFn = BOOL(WINAPI*)(HANDLE, HMODULE*, DWORD, LPDWORD);

static bool ContainsInsensitiveStr(const wchar_t* str, const wchar_t* sub) noexcept {
    if (!str || !sub) return false;
    const size_t len = std::wcslen(str);
    const size_t subLen = std::wcslen(sub);
    if (len < subLen) return false;
    for (size_t i = 0; i <= len - subLen; ++i) {
        bool match = true;
        for (size_t j = 0; j < subLen; ++j) {
            wchar_t a = str[i + j];
            wchar_t b = sub[j];
            if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
            if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
            if (a != b) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

void MfgModuleWatcher::ScanAndPatchLoadedModules() {
    auto k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    auto enumFn = reinterpret_cast<EnumProcessModulesFn>(
        GetProcAddress(k32, "K32EnumProcessModules"));
    if (!enumFn) {
        enumFn = reinterpret_cast<EnumProcessModulesFn>(
            GetProcAddress(k32, "EnumProcessModules"));
    }
    if (!enumFn) return;

    HMODULE modules[256];
    DWORD needed = 0;
    if (enumFn(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        const DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH] = {};
            if (GetModuleFileNameW(modules[i], path, MAX_PATH) > 0) {
                if (ContainsInsensitiveStr(path, L"\\models\\dlssg\\") ||
                    ContainsInsensitiveStr(path, L"/models/dlssg/") ||
                    ContainsInsensitiveStr(path, L"nvngx_dlssg")) {
                    DlssgTransfusion::Instance().TryApply(modules[i]);
                    observed_.store(true, std::memory_order_release);
                } else if (ContainsInsensitiveStr(path, L"sl.interposer")) {
                    StreamlineDlssgHook::Instance().Install(modules[i]);
                }
            }
        }
    }
}

void CALLBACK MfgModuleWatcher::Notification(
    unsigned long reason, const void* opaque, void* context) {
    if (reason != 1 || !opaque || !context) return;
    const auto* data = static_cast<const NotificationData*>(opaque);

    const bool isStreamline =
        EqualsInsensitive(data->loaded.baseDllName, L"sl.interposer.dll") ||
        ContainsInsensitive(data->loaded.baseDllName, L"sl.interposer");
    if (isStreamline) {
        auto* self = static_cast<MfgModuleWatcher*>(context);
        if (self->wakeEvent_) SetEvent(self->wakeEvent_);
    }

    const bool isDlssg =
        EqualsInsensitive(data->loaded.baseDllName, L"nvngx_dlssg.dll") ||
        EqualsInsensitive(data->loaded.baseDllName, L"_nvngx_dlssg.dll") ||
        ContainsInsensitive(data->loaded.baseDllName, L"dlssg") ||
        ContainsInsensitive(data->loaded.fullDllName, L"\\models\\dlssg\\") ||
        ContainsInsensitive(data->loaded.fullDllName, L"/models/dlssg/");
    if (!isDlssg)
        return;
    auto* self = static_cast<MfgModuleWatcher*>(context);
    auto* moduleBase = static_cast<HMODULE>(const_cast<void*>(data->loaded.dllBase));
    if (moduleBase) {
        DlssgTransfusion::Instance().TryApply(moduleBase);
        self->observed_.store(true, std::memory_order_release);
    }
    HMODULE pinned = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(data->loaded.dllBase),
            &pinned) && pinned) {
        self->QueueLoadedModule(pinned);
    }
}

void MfgModuleWatcher::QueueLoadedModule(HMODULE module) noexcept {
    if (!module || !running_.load()) {
        if (module) FreeLibrary(module);
        return;
    }
    HMODULE previous = pending_.exchange(module, std::memory_order_acq_rel);
    if (previous && previous != module) {
        FreeLibrary(previous);
    }
    observed_.store(true, std::memory_order_release);
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void MfgModuleWatcher::ThreadProc() {
    while (running_.load()) {
        const DWORD wait = WaitForSingleObject(wakeEvent_, INFINITE);
        if (wait != WAIT_OBJECT_0 || !running_.load()) break;

        HMODULE pending =
            pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!pending) {
            StreamlineDlssgHook::Instance().Install();
            continue;
        }

        DlssgTransfusion::Instance().TryApply(pending);
        FreeLibrary(pending);
        StreamlineDlssgHook::Instance().Install();
    }
}

} // namespace nrfusion
