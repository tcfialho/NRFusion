#include "nrfusion/CaptureProvider32.hpp"
#include "nrfusion/HostServer64.hpp"

#include <cassert>
#include <chrono>
#include <thread>

using namespace nrfusion;

int main() {
    const uint32_t pipePid = GetCurrentProcessId() + 700;
    char pipeName[128]{};
    FormatPipeName(pipeName, sizeof(pipeName), pipePid);

    HANDLE stalledPipe = CreateNamedPipeA(
        pipeName, PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 4096, 0, nullptr);
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
    assert(!client.IsConnected());
    stalledServer.join();

    HostServer64 host;
    assert(host.Start(pipePid));

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
    host.Stop();
    return 0;
}
