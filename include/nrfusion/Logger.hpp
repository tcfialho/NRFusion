#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>

namespace nrfusion {

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance() noexcept;

    void Initialize(HMODULE dllModule = nullptr) noexcept;
    void Log(LogLevel level, const char* component, const char* format, ...) noexcept;
    void Flush() noexcept;

    const std::wstring& GetLogPath() const noexcept { return logPath_; }

private:
    Logger() noexcept = default;
    ~Logger() noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void OpenLogFileLocked() noexcept;

    CRITICAL_SECTION cs_{};
    bool initialized_{false};
    HANDLE fileHandle_{INVALID_HANDLE_VALUE};
    std::wstring logPath_{};
};

#define NRF_LOG_INFO(comp, fmt, ...)  ::nrfusion::Logger::Instance().Log(::nrfusion::LogLevel::Info, comp, fmt, ##__VA_ARGS__)
#define NRF_LOG_WARN(comp, fmt, ...)  ::nrfusion::Logger::Instance().Log(::nrfusion::LogLevel::Warning, comp, fmt, ##__VA_ARGS__)
#define NRF_LOG_ERROR(comp, fmt, ...) ::nrfusion::Logger::Instance().Log(::nrfusion::LogLevel::Error, comp, fmt, ##__VA_ARGS__)
#define NRF_LOG_DEBUG(comp, fmt, ...) ::nrfusion::Logger::Instance().Log(::nrfusion::LogLevel::Debug, comp, fmt, ##__VA_ARGS__)

} // namespace nrfusion
