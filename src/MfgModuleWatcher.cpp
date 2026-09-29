#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/MfgModuleWatcher.hpp"
#include "nrfusion/DlssgTransfusion.hpp"

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
    if (const auto loaded = GetModuleHandleW(L"nvngx_dlssg.dll")) {
        DlssgTransfusion::Instance().TryApply(loaded);
        QueueLoadedModule(loaded);
    }
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
    if (!isProcessTerminating && wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
}

void CALLBACK MfgModuleWatcher::Notification(
    unsigned long reason, const void* opaque, void* context) {
    if (reason != 1 || !opaque || !context) return;
    const auto* data = static_cast<const NotificationData*>(opaque);
    const bool isDlssg =
        EqualsInsensitive(data->loaded.baseDllName, L"nvngx_dlssg.dll") ||
        ContainsInsensitive(data->loaded.baseDllName, L"dlssg") ||
        ContainsInsensitive(data->loaded.fullDllName, L"dlssg");
    if (!isDlssg)
        return;
    auto* self = static_cast<MfgModuleWatcher*>(context);
    const auto module = static_cast<HMODULE>(data->loaded.dllBase);
    self->QueueLoadedModule(module);
}

void MfgModuleWatcher::QueueLoadedModule(HMODULE module) noexcept {
    if (!module || !running_.load()) return;
    pending_.store(module, std::memory_order_release);
    observed_.store(true, std::memory_order_release);
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void MfgModuleWatcher::ThreadProc() {
    while (running_.load()) {
        const DWORD wait = WaitForSingleObject(wakeEvent_, INFINITE);
        if (wait != WAIT_OBJECT_0 || !running_.load()) break;

        Sleep(10);

        HMODULE pending =
            pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!pending) continue;

        DlssgTransfusion::Instance().TryApply(pending);
    }
}

} // namespace nrfusion
