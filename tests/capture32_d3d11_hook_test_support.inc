namespace {

struct SpawnedHost {
    HANDLE process = nullptr;

    ~SpawnedHost() {
        if (!process) return;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }
};

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

bool Fail(const char* message) {
    std::cerr << "[Capture32 D3D11 Hook] FAIL: " << message << "\n";
    return false;
}

std::string ResolveHostPath() {
    char configuredPath[MAX_PATH]{};
    const DWORD configuredLength = GetEnvironmentVariableA("NRFUSION_HOST64_PATH", configuredPath,
                                                            static_cast<DWORD>(sizeof(configuredPath)));
    return configuredLength > 0 && configuredLength < sizeof(configuredPath) ? configuredPath : std::string{};
}

bool StartHost(SpawnedHost& host) {
    const std::string hostPath = ResolveHostPath();
    if (hostPath.empty() || GetFileAttributesA(hostPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail("set NRFUSION_HOST64_PATH to the x64 NRFusionHost64.exe");
    }

    std::string commandLine = "\"" + hostPath + "\" " + std::to_string(GetCurrentProcessId());
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return Fail("CreateProcessA for NRFusionHost64.exe failed");
    }
    CloseHandle(process.hThread);
    host.process = process.hProcess;
    return true;
}

bool PresentColor(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                  const float color[4]) {
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    return SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) &&
        SUCCEEDED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget)) &&
        (context->ClearRenderTargetView(renderTarget.Get(), color), true) &&
        SUCCEEDED(swapChain->Present(0, 0));
}

// Binds a real depth-stencil view (created typeless, the only shape the capture hook can read as
// an SRV) through OMSetRenderTargets before presenting, exercising the depth capture heuristic.
bool PresentColorWithDepth(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                           ID3D11DepthStencilView* depthStencilView, const float color[4], float depthValue) {
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
        FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget))) {
        return false;
    }
    ID3D11RenderTargetView* rtvs[1] = { renderTarget.Get() };
    context->OMSetRenderTargets(1, rtvs, depthStencilView);
    context->ClearRenderTargetView(renderTarget.Get(), color);
    context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, depthValue, 0);
    return SUCCEEDED(swapChain->Present(0, 0));
}

} // namespace

