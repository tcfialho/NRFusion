HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    const PresentFn original = OriginalPresent(swapChain);
    if (!original) return DXGI_ERROR_INVALID_CALL;
    if (swapChain) {
        DXGI_SWAP_CHAIN_DESC scd{};
        if (SUCCEEDED(swapChain->GetDesc(&scd)) && scd.OutputWindow) {
            RuntimeOverlay::Instance().Initialize(scd.OutputWindow);
        }
    }
    g_captureSession.OnPresent(swapChain);
    RuntimeOverlayD3D12::Instance().Draw(swapChain, flags);
    return original(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                               UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
    const ResizeBuffersFn original = OriginalResizeBuffers(swapChain);
    if (!original) return DXGI_ERROR_INVALID_CALL;
    if (!RuntimeOverlayD3D12::Instance().BeforeResize(swapChain)) return DXGI_ERROR_WAS_STILL_DRAWING;
    g_captureSession.OnResize(swapChain);
    return original(swapChain, bufferCount, width, height, newFormat, swapChainFlags);
}

HRESULT WINAPI HookedCreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType,
                                              HMODULE software, UINT flags,
                                              const D3D_FEATURE_LEVEL* featureLevels,
                                              UINT featureLevelCount, UINT sdkVersion,
                                              const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
                                              IDXGISwapChain** swapChain, ID3D11Device** device,
                                              D3D_FEATURE_LEVEL* selectedFeatureLevel,
                                              ID3D11DeviceContext** immediateContext) {
    const CreateDeviceAndSwapChainFn original = g_originalCreateDeviceAndSwapChain.load();
    if (!original) return E_FAIL;

    const HRESULT result = original(adapter, driverType, software, flags, featureLevels, featureLevelCount,
                                    sdkVersion, swapChainDesc, swapChain, device, selectedFeatureLevel,
                                    immediateContext);
    if (SUCCEEDED(result) && swapChain && *swapChain) {
        PatchSwapChainVtable(*swapChain);
        if (immediateContext && *immediateContext) PatchContextVtable(*immediateContext);
        g_captureSession.Attach(device ? *device : nullptr, immediateContext ? *immediateContext : nullptr, *swapChain);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChain(IDXGIFactory* factory, IUnknown* device,
                                                        DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain) {
    const DxgiCreateSwapChainFn original = OriginalFactoryCreateSwapChain(factory);
    if (!original) return E_FAIL;
    const HRESULT result = original(factory, device, desc, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) AttachFromDeviceUnknown(device, *swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                               const DXGI_SWAP_CHAIN_DESC1* desc,
                                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                               IDXGIOutput* restrictToOutput,
                                                               IDXGISwapChain1** swapChain) {
    const DxgiCreateSwapChainForHwndFn original = OriginalFactoryCreateSwapChainForHwnd(factory);
    if (!original) return E_FAIL;
    const HRESULT result = original(factory, device, hwnd, desc, fullscreenDesc, restrictToOutput, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) AttachFromDeviceUnknown(device, *swapChain);
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** factory);
HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** factory);
HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void** factory);

// One spec per IAT-hookable creation entry point. recordOriginal stashes the resolved original
// into that function's typed atomic so its Hooked* trampoline can call back into the real thing.
struct ImportHookSpec {
    const char* dllName;
    const char* functionName;
    void* hookFunction;
    void (*recordOriginal)(void* original);
};

// Walks one module's import table and patches every IAT slot matching any spec's {dllName,
// functionName}, across d3d11.dll (the monolithic path) and dxgi.dll (the factory-based split
// path) in a single pass, so both creation styles are covered by the same retry loop.
bool PatchModuleImports(HMODULE module, const ImportHookSpec* specs, size_t specCount) {
    HMODULE heldModule = nullptr;
    if (!module || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(module), &heldModule)) return false;
    struct ModuleReference {
        HMODULE handle;
        ~ModuleReference() { FreeLibrary(handle); }
    };
    const ModuleReference reference{heldModule};
#ifdef NRFUSION_PROXY_RUNTIME
    TryInstallGameNeuralHooks(heldModule);
#endif
    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) return false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    bool patched = false;
    for (; descriptor->Name != 0; ++descriptor) {
        const char* importedDll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (descriptor->OriginalFirstThunk == 0) continue;

        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* importedName = reinterpret_cast<const char*>(importByName->Name);

            for (size_t i = 0; i != specCount; ++i) {
                const ImportHookSpec& spec = specs[i];
                if (_stricmp(importedDll, spec.dllName) != 0) continue;
                if (std::strcmp(importedName, spec.functionName) != 0) continue;

                auto** slot = reinterpret_cast<void**>(&addresses->u1.Function);
                void* current = *slot;
                if (current == spec.hookFunction) {
                    patched = true;
                    break;
                }

                void* previous = nullptr;
                if (ReplacePointer(slot, spec.hookFunction, previous)) {
                    spec.recordOriginal(previous);
                    g_iatPatches.push_back({slot, previous});
                    patched = true;
                }
                break;
            }
        }
    }
    return patched;
}

void PatchLoadedD3D11Imports() {
    const ImportHookSpec specs[] = {
        {"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
         reinterpret_cast<void*>(&HookedCreateDeviceAndSwapChain), &RecordOriginalCreateDeviceAndSwapChain},
        {"dxgi.dll", "CreateDXGIFactory", reinterpret_cast<void*>(&HookedCreateDXGIFactory),
         &RecordOriginalCreateDXGIFactory},
        {"dxgi.dll", "CreateDXGIFactory1", reinterpret_cast<void*>(&HookedCreateDXGIFactory1),
         &RecordOriginalCreateDXGIFactory1},
        {"dxgi.dll", "CreateDXGIFactory2", reinterpret_cast<void*>(&HookedCreateDXGIFactory2),
         &RecordOriginalCreateDXGIFactory2},
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Module32First(snapshot, &entry)) {
        do {
            if (g_stopRequested) break;
            PatchModuleImports(entry.hModule, specs, std::size(specs));
        } while (Module32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** factory) {
    const CreateDxgiFactoryFn original = g_originalCreateDXGIFactory.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** factory) {
    const CreateDxgiFactoryFn original = g_originalCreateDXGIFactory1.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    const CreateDxgiFactory2Fn original = g_originalCreateDXGIFactory2.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(flags, riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

DWORD WINAPI CaptureHookWorker(void*) {
    for (unsigned attempt = 0; attempt != 100 && !g_stopRequested; ++attempt) {
        {
            std::scoped_lock lock(g_hookMutex);
            if (!g_stopRequested) PatchLoadedD3D11Imports();
        }
        if (g_originalCreateDeviceAndSwapChain.load() || g_originalCreateDXGIFactory.load() ||
            g_originalCreateDXGIFactory1.load() || g_originalCreateDXGIFactory2.load()) {
            if (attempt >= 30) break;
        }
        Sleep(100);
    }
    return 0;
}

} // namespace

#ifdef NRFUSION_PROXY_RUNTIME
void AttachProxyDxgiFactory(void* factory) {
    if (factory) PatchFactoryVtable(static_cast<IDXGIFactory*>(factory));
}
#endif

void StartCaptureD3D11Hooks() {
    bool expected = false;
    if (!g_hookWorkerStarted.compare_exchange_strong(expected, true)) return;

    g_stopRequested = false;
    HANDLE worker = CreateThread(nullptr, 0, CaptureHookWorker, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
}

void SetCaptureD3D11WorkingScale(float workingScale) {
    g_captureSession.SetWorkingScale(workingScale);
}

void SetCaptureD3D11ProcessingMode(uint32_t mode) {
    g_captureSession.SetProcessingMode(mode);
}

bool GetCaptureD3D11TransportInfo(CaptureD3D11TransportInfo& outInfo) {
    return g_captureSession.GetTransportInfo(outInfo);
}

void StopCaptureD3D11Hooks() {
    g_stopRequested = true;
    RuntimeOverlayD3D12::Instance().Reset();
    g_captureSession.Shutdown();
    std::scoped_lock lock(g_hookMutex);

    for (const IatPatch& patch : g_iatPatches) {
        if (!patch.slot) continue;
        HMODULE heldModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(patch.slot), &heldModule)) continue;
        const void* current = *patch.slot;
        if (current == reinterpret_cast<void*>(&HookedCreateDeviceAndSwapChain) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory1) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory2)) {
            RestorePointer(patch.slot, patch.original);
        }
        FreeLibrary(heldModule);
    }
    g_iatPatches.clear();

    for (const auto& [vtable, originals] : g_factoryVtables) {
        if (!vtable) continue;
        if (vtable[kFactoryCreateSwapChainIndex] == reinterpret_cast<void*>(&HookedFactoryCreateSwapChain)) {
            RestorePointer(&vtable[kFactoryCreateSwapChainIndex], reinterpret_cast<void*>(originals.createSwapChain));
        }
        if (originals.createSwapChainForHwnd &&
            vtable[kFactory2CreateSwapChainForHwndIndex] ==
                reinterpret_cast<void*>(&HookedFactoryCreateSwapChainForHwnd)) {
            RestorePointer(&vtable[kFactory2CreateSwapChainForHwndIndex],
                          reinterpret_cast<void*>(originals.createSwapChainForHwnd));
        }
    }
    g_factoryVtables.clear();

    for (const auto& [vtable, originals] : g_swapChainVtables) {
        if (!vtable) continue;
        if (vtable[kSwapChainPresentIndex] == reinterpret_cast<void*>(&HookedPresent)) {
            RestorePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(originals.present));
        }
        if (vtable[kSwapChainResizeBuffersIndex] == reinterpret_cast<void*>(&HookedResizeBuffers)) {
            RestorePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(originals.resizeBuffers));
        }
        if (originals.present1 && vtable[kSwapChainPresent1Index] == reinterpret_cast<void*>(&HookedPresent1))
            RestorePointer(&vtable[kSwapChainPresent1Index], reinterpret_cast<void*>(originals.present1));
    }
    g_swapChainVtables.clear();

    for (const auto& [vtable, originals] : g_contextVtables) {
        if (!vtable) continue;
        if (vtable[kContextOMSetRenderTargetsIndex] == reinterpret_cast<void*>(&HookedOMSetRenderTargets)) {
            RestorePointer(&vtable[kContextOMSetRenderTargetsIndex],
                          reinterpret_cast<void*>(originals.omSetRenderTargets));
        }
    }
    g_contextVtables.clear();
    g_hookWorkerStarted = false;
}

