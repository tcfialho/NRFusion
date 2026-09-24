#include "nrfusion/CaptureProvider32.hpp"

#include <cassert>
#include <chrono>
#include <thread>

using namespace nrfusion;

namespace {

HANDLE CreatePipe(uint32_t pipePid) {
    char pipeName[128]{};
    FormatPipeName(pipeName, sizeof(pipeName), pipePid);
    return CreateNamedPipeA(
        pipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
}

void RunHealthyServer(HANDLE pipe) {
    const BOOL connected = ConnectNamedPipe(pipe, nullptr);
    assert(connected || GetLastError() == ERROR_PIPE_CONNECTED);

    IpcHelloMessage hello{};
    DWORD read = 0;
    assert(ReadFile(pipe, &hello, sizeof(hello), &read, nullptr));
    assert(read == sizeof(hello));

    IpcHelloAckMessage helloAck{};
    helloAck.hostPid = GetCurrentProcessId();
    helloAck.connectionGeneration = 9;
    DWORD written = 0;
    assert(WriteFile(pipe, &helloAck, sizeof(helloAck), &written, nullptr));
    assert(written == sizeof(helloAck));

    IpcBuildMessage build{};
    assert(ReadFile(pipe, &build, sizeof(build), &read, nullptr));
    assert(read == sizeof(build));

    IpcBuildAckMessage buildAck{};
    buildAck.sessionId = build.sessionId;
    buildAck.connectionGeneration = build.connectionGeneration;
    buildAck.workWidth = build.width;
    buildAck.workHeight = build.height;
    assert(WriteFile(pipe, &buildAck, sizeof(buildAck), &written, nullptr));
    assert(written == sizeof(buildAck));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

} // namespace

int main() {
    const uint32_t pipePid = GetCurrentProcessId() + 700;
    HANDLE stalledPipe = CreatePipe(pipePid);
    assert(stalledPipe != INVALID_HANDLE_VALUE);

    std::thread stalledServer([&] {
        const BOOL connected = ConnectNamedPipe(stalledPipe, nullptr);
        assert(connected || GetLastError() == ERROR_PIPE_CONNECTED);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        DisconnectNamedPipe(stalledPipe);
        CloseHandle(stalledPipe);
    });

    CaptureProvider32 client;
    const auto started = std::chrono::steady_clock::now();
    assert(!client.Connect(pipePid, 100));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    assert(elapsed.count() < 1000);
    stalledServer.join();

    HANDLE healthyPipe = CreatePipe(pipePid);
    assert(healthyPipe != INVALID_HANDLE_VALUE);
    std::thread healthyServer(RunHealthyServer, healthyPipe);

    bool reconnected = false;
    for (int attempt = 0; attempt != 100 && !reconnected; ++attempt) {
        reconnected = client.Connect(pipePid, 1000);
        if (!reconnected)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(reconnected);

    CaptureClientConfig config{};
    config.width = 64;
    config.height = 64;
    config.workingScale = 1.0f;
    assert(client.Configure(config));

    client.Disconnect();
    healthyServer.join();
    return 0;
}
