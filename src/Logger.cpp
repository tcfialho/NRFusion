#include "nrfusion/Logger.hpp"

#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace nrfusion {

Logger& Logger::Instance() noexcept {
    static Logger instance;
    return instance;
}

Logger::~Logger() noexcept {
    if (initialized_) {
        EnterCriticalSection(&cs_);
        if (fileHandle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(fileHandle_);
            fileHandle_ = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&cs_);
        DeleteCriticalSection(&cs_);
        initialized_ = false;
    }
}

void Logger::Initialize(HMODULE dllModule) noexcept {
    if (!initialized_) {
        InitializeCriticalSection(&cs_);
        initialized_ = true;
    }

    EnterCriticalSection(&cs_);
    if (fileHandle_ == INVALID_HANDLE_VALUE) {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD len = 0;
        if (dllModule) {
            len = GetModuleFileNameW(dllModule, modulePath, MAX_PATH);
        }
        if (len == 0) {
            len = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        }

        std::filesystem::path p(modulePath);
        p.remove_filename();
        p /= L"nrfusion.log";
        logPath_ = p.wstring();

        fileHandle_ = CreateFileW(
            logPath_.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
    }
    LeaveCriticalSection(&cs_);
}

void Logger::OpenLogFileLocked() noexcept {
    if (fileHandle_ != INVALID_HANDLE_VALUE) return;

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path p(modulePath);
    p.remove_filename();
    p /= L"nrfusion.log";
    logPath_ = p.wstring();

    fileHandle_ = CreateFileW(
        logPath_.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

void Logger::Log(LogLevel level, const char* component, const char* format, ...) noexcept {
    if (!initialized_) {
        Initialize(nullptr);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    const char* levelStr = "INFO";
    switch (level) {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO"; break;
        case LogLevel::Warning: levelStr = "WARN"; break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
    }

    const DWORD pid = GetCurrentProcessId();
    const DWORD tid = GetCurrentThreadId();

    char messageBuf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(messageBuf, sizeof(messageBuf), format, args);
    va_end(args);

    char lineBuf[2560];
    int lineLen = snprintf(
        lineBuf, sizeof(lineBuf),
        "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%u:%u] [%s] [%s] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        pid, tid, levelStr, component ? component : "General", messageBuf
    );

    if (lineLen <= 0) return;

    // OutputDebugString for attached debuggers / DebugView
    OutputDebugStringA(lineBuf);

    EnterCriticalSection(&cs_);
    OpenLogFileLocked();
    if (fileHandle_ != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(fileHandle_, lineBuf, static_cast<DWORD>(lineLen), &bytesWritten, nullptr);
        FlushFileBuffers(fileHandle_);
    }
    LeaveCriticalSection(&cs_);
}

void Logger::Flush() noexcept {
    if (!initialized_) return;
    EnterCriticalSection(&cs_);
    if (fileHandle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(fileHandle_);
    }
    LeaveCriticalSection(&cs_);
}

} // namespace nrfusion
