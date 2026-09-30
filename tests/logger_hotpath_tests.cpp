#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

namespace {
unsigned writeCalls = 0;
unsigned flushCalls = 0;
BOOL WINAPI CountedWriteFile(HANDLE file, LPCVOID bytes, DWORD size, LPDWORD written, LPOVERLAPPED overlapped) {
    ++writeCalls;
    return WriteFile(file, bytes, size, written, overlapped);
}
BOOL WINAPI CountedFlush(HANDLE file) {
    ++flushCalls;
    return FlushFileBuffers(file);
}
}

#define WriteFile CountedWriteFile
#define FlushFileBuffers CountedFlush
#include "../src/Logger.cpp"
#undef WriteFile
#undef FlushFileBuffers

int main(int argc, char** argv) {
    auto& logger = nrfusion::Logger::Instance();
    logger.Initialize();
    if (argc == 2) {
        std::array<double, 1000> microseconds{};
        for (unsigned index = 0; index < 100; ++index)
            logger.Log(nrfusion::LogLevel::Info, "LoggerBench", "warmup=%u", index);
        const unsigned initialWrites = writeCalls;
        const unsigned initialFlushes = flushCalls;
        for (unsigned index = 0; index < microseconds.size(); ++index) {
            const auto start = std::chrono::steady_clock::now();
            logger.Log(nrfusion::LogLevel::Info, "LoggerBench", "frame=%u status=NR applied", index);
            microseconds[index] = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start).count();
        }
        std::ofstream output(argv[1]);
        output << "cpu_log_us\n";
        for (double sample : microseconds) output << sample << '\n';
        assert(output.good());
        std::cout << "writes=" << writeCalls - initialWrites
                  << " flushes=" << flushCalls - initialFlushes << '\n';
        logger.Flush();
        return 0;
    }
    const unsigned initialFlushes = flushCalls;
    const unsigned initialWrites = writeCalls;
    for (unsigned index = 0; index < 100; ++index)
        logger.Log(nrfusion::LogLevel::Info, "LoggerTest", "info=%u", index);
    logger.Log(nrfusion::LogLevel::Debug, "LoggerTest", "debug marker");
    logger.Log(nrfusion::LogLevel::Warning, "LoggerTest", "warning marker");
    assert(writeCalls - initialWrites == 102);
    assert(flushCalls == initialFlushes);
    logger.Log(nrfusion::LogLevel::Error, "LoggerTest", "error marker");
    assert(flushCalls == initialFlushes + 1);
    logger.Flush();
    assert(flushCalls == initialFlushes + 2);
    std::ifstream input(logger.GetLogPath());
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    for (const char* marker : {"debug marker", "warning marker", "error marker", "info=99"})
        assert(contents.find(marker) != std::string::npos);
    return 0;
}
