    const auto setDpiContext = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (setDpiContext) {
        const BOOL dpiOk = setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::cout << "[DPI] SetProcessDpiAwarenessContext: " << (dpiOk ? "OK" : "FAIL")
                  << " (err=" << GetLastError() << ")" << std::endl;
    }
    HDESK inputDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (inputDesk) {
        const BOOL deskOk = SetThreadDesktop(inputDesk);
        std::cout << "[DESK] SetThreadDesktop: " << (deskOk ? "OK" : "FAIL")
                  << " (err=" << GetLastError() << ")" << std::endl;
    }

    auto EnsureWindowFocus = [](HWND hwnd) -> bool {
        if (!hwnd || !IsWindow(hwnd)) return false;
        DWORD fgPid = 0;
        HWND fgWnd = GetForegroundWindow();
        if (fgWnd) GetWindowThreadProcessId(fgWnd, &fgPid);
        if (fgPid == GetCurrentProcessId()) return true;

        const DWORD currentThreadId = GetCurrentThreadId();
        const DWORD fgThreadId = fgWnd ? GetWindowThreadProcessId(fgWnd, nullptr) : 0;
        if (fgThreadId && fgThreadId != currentThreadId) {
            AttachThreadInput(currentThreadId, fgThreadId, TRUE);
        }

        keybd_event(VK_MENU, 0, 0, 0);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);

        ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);

        if (fgThreadId && fgThreadId != currentThreadId) {
            AttachThreadInput(currentThreadId, fgThreadId, FALSE);
        }

        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        fgWnd = GetForegroundWindow();
        if (fgWnd) GetWindowThreadProcessId(fgWnd, &fgPid);
        return (fgPid == GetCurrentProcessId());
    };

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NRFusionRequiem";
    RegisterClassExW(&windowClass);

    const UINT width = static_cast<UINT>(g_width), height = static_cast<UINT>(g_height);
    UINT inputWidth = width * 2 / 3, inputHeight = height * 2 / 3;
    RECT rect{0, 0, (LONG) width, (LONG) height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"NRFusion Requiem Testbed",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!g_hwnd) {
        std::cout << "[ERRO] Nao consegui criar a janela." << std::endl;
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOW);

    HMODULE standaloneProxy = nullptr;
    if (g_requireNrfusionProxy || g_mfgMultiplier >= 2) {
        wchar_t proxyPath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, proxyPath, MAX_PATH);
        if (wchar_t* slash = wcsrchr(proxyPath, L'\\'))
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - proxyPath), L"version.dll");
        standaloneProxy = LoadLibraryW(proxyPath);
        if (!standaloneProxy) {
            std::cerr << "[ERRO] standalone version.dll nao carregou." << std::endl;
            return 1;
        }
        using EnsureRuntimeFn = void(__cdecl*)();
        const auto ensureRuntime = reinterpret_cast<EnsureRuntimeFn>(
            GetProcAddress(standaloneProxy, "NRFusion_EnsureRuntime"));
        if (ensureRuntime) ensureRuntime();
    }

    requiem::StreamlineMfg mfg;
    if (!mfg.Initialize(g_mfgMultiplier, g_requireMfg)) {
        std::cerr << "[MFG] " << mfg.Status() << std::endl;
        if (g_requireMfg) return 1;
    }
    mfg.SetWindow(g_hwnd);
    if (g_mfgMultiplier > 2 && standaloneProxy) {
        using MfgFlagFn = int(__cdecl*)();
        using MfgMaxFn = unsigned int(__cdecl*)();
        const auto observed = reinterpret_cast<MfgFlagFn>(
            GetProcAddress(standaloneProxy, "NRFusion_MfgModuleObserved"));
        const auto qualified = reinterpret_cast<MfgFlagFn>(
            GetProcAddress(standaloneProxy, "NRFusion_MfgPatchQualified"));
        const auto unlockedMax = reinterpret_cast<MfgMaxFn>(
            GetProcAddress(standaloneProxy, "NRFusion_MfgUnlockedMax"));
        for (unsigned attempt = 0;
             attempt < 2000 && qualified && !qualified(); ++attempt)
            Sleep(1);
        if (!observed || !qualified || !unlockedMax ||
            !observed() || !qualified() ||
            unlockedMax() < g_mfgMultiplier - 1) {
            using ReasonFn = const char*(__cdecl*)();
            const auto reasonFn = reinterpret_cast<ReasonFn>(
                GetProcAddress(standaloneProxy, "NRFusion_MfgFailureReason"));
            std::cerr << "[ERRO] proxy NRFusion nao qualificou late-load DLSS-G: "
                      << "observed=" << (observed ? observed() : -1)
                      << " qualified=" << (qualified ? qualified() : -1)
                      << " unlockedMax=" << (unlockedMax ? unlockedMax() : 0)
                      << " reason=" << (reasonFn ? reasonFn() : "none")
                      << std::endl;
            return 1;
        }
    }

    if (g_debugLayer) {
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer unavailable");
        debug->EnableDebugLayer();
    }

    ComPtr<IDXGIFactory4> nativeFactory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&nativeFactory)), "CreateDXGIFactory2");

    ComPtr<IDXGIAdapter1> selectedAdapter;
    if (mfg.Requested()) {
        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(nativeFactory.As(&factory6))) {
            for (UINT i = 0; ; ++i) {
                ComPtr<IDXGIAdapter1> candidate;
                if (factory6->EnumAdapterByGpuPreference(
                        i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                        IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                candidate->GetDesc1(&desc);
                if (desc.VendorId == 0x10DE &&
                    SUCCEEDED(D3D12CreateDevice(
                        candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                        __uuidof(ID3D12Device), nullptr))) {
                    selectedAdapter = candidate;
                    char descUtf8[256] = {};
                    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, descUtf8, sizeof(descUtf8), nullptr, nullptr);
                    std::cout << "[MFG] adapter: " << descUtf8 << std::endl;
                    break;
                }
            }
        }
    }

    ComPtr<ID3D12Device> nativeDevice;
    Check(D3D12CreateDevice(selectedAdapter.Get(), D3D_FEATURE_LEVEL_12_0,
                            IID_PPV_ARGS(&nativeDevice)), "D3D12CreateDevice");
    ComPtr<IDXGIOutput> desktopOutput;
    if (selectedAdapter) {
        selectedAdapter->EnumOutputs(0, &desktopOutput);
    }
    if (!desktopOutput && nativeFactory) {
        for (UINT a = 0; ; ++a) {
            ComPtr<IDXGIAdapter1> ad;
            if (nativeFactory->EnumAdapters1(a, &ad) == DXGI_ERROR_NOT_FOUND) break;
            ComPtr<IDXGIOutput> out;
            if (SUCCEEDED(ad->EnumOutputs(0, &out))) {
                DXGI_OUTPUT_DESC d{};
                if (SUCCEEDED(out->GetDesc(&d)) && d.AttachedToDesktop) {
                    desktopOutput = out;
                    break;
                }
            }
        }
    }
    if (desktopOutput) {
        DXGI_OUTPUT_DESC outputDesc{};
        if (SUCCEEDED(desktopOutput->GetDesc(&outputDesc)) && outputDesc.AttachedToDesktop) {
            const RECT area = outputDesc.DesktopCoordinates;
            const int monWidth = area.right - area.left;
            const int monHeight = area.bottom - area.top;
            const int posX = area.left + (std::max)(0, (monWidth - static_cast<int>(width)) / 2);
            const int posY = area.top + (std::max)(0, (monHeight - static_cast<int>(height)) / 2);
            std::cout << "[MFG] desktop bounds: (" << area.left << "," << area.top << ")-("
                      << area.right << "," << area.bottom << ") size " << monWidth << "x" << monHeight
                      << " -> window pos: (" << posX << "," << posY << ")" << std::endl;
            SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(
                g_hwnd, HWND_TOPMOST, posX, posY,
                width, height,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            char outUtf8[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, outputDesc.DeviceName, -1, outUtf8, sizeof(outUtf8), nullptr, nullptr);
            std::cout << "[MFG] output: " << outUtf8 << std::endl;
        }
    }

    bool isFocused = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (EnsureWindowFocus(g_hwnd)) {
            isFocused = true;
            break;
        }
        Sleep(10);
    }
    std::cout << "[MFG] window focus: " << (isFocused ? "YES" : "NO") << std::endl;
    ID3D12Device* deviceApi = nativeDevice.Get();
    if (mfg.Requested() && !mfg.SetDevice(nativeDevice.Get(), &deviceApi)) {
        std::cerr << "[MFG] " << mfg.Status() << std::endl;
        return 1;
    }
    ComPtr<ID3D12Device> proxyDevice;
    if (deviceApi == nativeDevice.Get()) proxyDevice = nativeDevice;
    else proxyDevice.Attach(deviceApi);
    // Manual Streamline hooking: native interfaces everywhere except
    // explicitly hooked APIs. Only CreateCommandQueue uses the proxy device.
    ComPtr<ID3D12Device> device = nativeDevice;

    IDXGIFactory4* factoryApi = nativeFactory.Get();
    if (mfg.Requested() && !mfg.UpgradeFactory(nativeFactory.Get(), &factoryApi)) {
        std::cerr << "[MFG] " << mfg.Status() << std::endl;
        return 1;
    }
    ComPtr<IDXGIFactory4> factory;
    if (factoryApi == nativeFactory.Get()) factory = nativeFactory;
    else factory.Attach(factoryApi);

    BOOL tearingSupported = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(nativeFactory.As(&factory5)))
        factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &tearingSupported, sizeof(tearingSupported));
    const bool allowTearing = tearingSupported == TRUE;

    ComPtr<ID3D12InfoQueue> debugMessages;
    if (g_debugLayer) Check(nativeDevice.As(&debugMessages), "D3D12 info queue");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(proxyDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount = kBackBuffers;
    swapDesc.Width = width;
    swapDesc.Height = height;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    ComPtr<IDXGISwapChain1> swapChain1;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), g_hwnd, &swapDesc, nullptr, nullptr, &swapChain1),
          "CreateSwapChainForHwnd");
    ComPtr<IDXGISwapChain3> swapChain;
    Check(swapChain1.As(&swapChain), "IDXGISwapChain3");
    if (mfg.Requested() &&
        !mfg.Configure(width, height, inputWidth, inputHeight, kBackBuffers)) {
        std::cerr << "[MFG] " << mfg.Status() << std::endl;
        return 1;
    }

#include "requiem_frame_resources.inc"

#include "requiem_run_setup_post.inc"
