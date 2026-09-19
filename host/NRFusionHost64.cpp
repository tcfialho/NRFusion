#include "nrfusion/HostServer64.hpp"

#include <csignal>
#include <iostream>
#include <string>

static std::atomic<bool> g_quit{ false };

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_quit = true;
        return TRUE;
    default:
        return FALSE;
    }
}

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    if (argc < 2) {
        std::cout << "NRFusionHost64 - Headless x64 Neural Rendering Server\n";
        std::cout << "Usage:\n";
        std::cout << "  NRFusionHost64.exe <client_pid>\n";
        std::cout << "  NRFusionHost64.exe --pid <client_pid>\n";
        std::cout << "  NRFusionHost64.exe --test\n";
        return 1;
    }

    std::string arg1 = argv[1];
    if (arg1 == "--test") {
        std::cout << "[Host64] Running standalone headless self-test...\n";
        nrfusion::HostServer64 server;
        if (!server.InitializeD3D12()) {
            std::cout << "[Host64] D3D12 device initialization unavailable on this adapter (fallback mode).\n";
        } else {
            std::cout << "[Host64] D3D12 device and SyntheticDx12Provider successfully initialized.\n";
        }
        uint32_t selfPid = GetCurrentProcessId();
        if (!server.Start(selfPid)) {
            std::cerr << "[Host64] Failed to start server on PID " << selfPid << "\n";
            return 1;
        }
        std::cout << "[Host64] Server started successfully on pipe \\\\.\\pipe\\nrfusion-ipc." << selfPid << "\n";
        server.Stop();
        std::cout << "[Host64] Server stopped cleanly. Self-test PASSED.\n";
        return 0;
    }

    uint32_t targetPid = 0;
    if (arg1 == "--pid" && argc >= 3) {
        targetPid = static_cast<uint32_t>(std::stoul(argv[2]));
    } else {
        targetPid = static_cast<uint32_t>(std::stoul(arg1));
    }

    if (targetPid == 0) {
        std::cerr << "[Host64] Invalid PID specified.\n";
        return 1;
    }

    std::cout << "[Host64] Starting headless server for client PID " << targetPid << "...\n";
    nrfusion::HostServer64 server;
    if (!server.Start(targetPid)) {
        std::cerr << "[Host64] Failed to create server pipe for PID " << targetPid << "\n";
        return 1;
    }

    std::cout << "[Host64] Listening on \\\\.\\pipe\\nrfusion-ipc." << targetPid << "\n";

    HANDLE hClientProcess = OpenProcess(SYNCHRONIZE, FALSE, targetPid);

    while (!g_quit && server.IsRunning()) {
        if (hClientProcess) {
            DWORD waitRes = WaitForSingleObject(hClientProcess, 200);
            if (waitRes == WAIT_OBJECT_0) {
                std::cout << "[Host64] Client process terminated. Exiting...\n";
                break;
            }
        } else {
            Sleep(200);
        }
    }
    if (hClientProcess) CloseHandle(hClientProcess);

    std::cout << "[Host64] Stopping server (processed " << server.ProcessedFrameCount() << " frames)...\n";
    server.Stop();
    std::cout << "[Host64] Shutdown complete.\n";
    return 0;
}
