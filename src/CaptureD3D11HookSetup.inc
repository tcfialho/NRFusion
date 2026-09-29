std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_hookWorkerStarted{false};
std::atomic<CreateDeviceAndSwapChainFn> g_originalCreateDeviceAndSwapChain{nullptr};
std::atomic<CreateDxgiFactoryFn> g_originalCreateDXGIFactory{nullptr};
std::atomic<CreateDxgiFactoryFn> g_originalCreateDXGIFactory1{nullptr};
std::atomic<CreateDxgiFactory2Fn> g_originalCreateDXGIFactory2{nullptr};
std::mutex g_hookMutex;
std::unordered_map<void**, VtableOriginals> g_swapChainVtables;
std::unordered_map<void**, ContextVtableOriginals> g_contextVtables;
std::unordered_map<void**, FactoryVtableOriginals> g_factoryVtables;
std::vector<IatPatch> g_iatPatches;
D3D11CaptureSession g_captureSession;

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                               UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags);
void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                                 ID3D11RenderTargetView* const* renderTargetViews,
                                                 ID3D11DepthStencilView* depthStencilView);
HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChain(IDXGIFactory* factory, IUnknown* device,
                                                        DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain);
HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                               const DXGI_SWAP_CHAIN_DESC1* desc,
                                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                               IDXGIOutput* restrictToOutput,
                                                               IDXGISwapChain1** swapChain);

bool ReplacePointer(void** slot, void* replacement, void*& previous) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return false;
    previous = InterlockedExchangePointer(reinterpret_cast<PVOID*>(slot), replacement);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

void RestorePointer(void** slot, void* original) {
    if (!slot || !original) return;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return;
    InterlockedExchangePointer(reinterpret_cast<PVOID*>(slot), original);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

bool PatchSwapChainVtable(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_swapChainVtables.contains(vtable)) return !g_stopRequested;

    VtableOriginals originals{
        reinterpret_cast<PresentFn>(vtable[kSwapChainPresentIndex]),
        reinterpret_cast<ResizeBuffersFn>(vtable[kSwapChainResizeBuffersIndex]),
    };
    if (!originals.present || !originals.resizeBuffers) return false;

    g_swapChainVtables.emplace(vtable, originals);
    void* previousPresent = nullptr;
    void* previousResize = nullptr;
    if (!ReplacePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(&HookedPresent), previousPresent) ||
        !ReplacePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(&HookedResizeBuffers), previousResize)) {
        RestorePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(originals.present));
        RestorePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(originals.resizeBuffers));
        g_swapChainVtables.erase(vtable);
        return false;
    }
    return true;
}

bool PatchContextVtable(ID3D11DeviceContext* context) {
    if (!context) return false;
    void** vtable = *reinterpret_cast<void***>(context);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_contextVtables.contains(vtable)) return !g_stopRequested;

    ContextVtableOriginals originals{
        reinterpret_cast<OMSetRenderTargetsFn>(vtable[kContextOMSetRenderTargetsIndex]),
    };
    if (!originals.omSetRenderTargets) return false;

    g_contextVtables.emplace(vtable, originals);
    void* previous = nullptr;
    if (!ReplacePointer(&vtable[kContextOMSetRenderTargetsIndex], reinterpret_cast<void*>(&HookedOMSetRenderTargets),
                        previous)) {
        g_contextVtables.erase(vtable);
        return false;
    }
    return true;
}

// factory may be any IDXGIFactory (or later revision); CreateSwapChainForHwnd's slot only exists
// once the object actually implements IDXGIFactory2, so that slot is patched only after a
// QueryInterface confirms it - writing slot 15 on a bare IDXGIFactory vtable would be OOB.
bool PatchFactoryVtable(IDXGIFactory* factory) {
    if (!factory) return false;
    void** vtable = *reinterpret_cast<void***>(factory);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_factoryVtables.contains(vtable)) return !g_stopRequested;

    FactoryVtableOriginals originals{
        reinterpret_cast<DxgiCreateSwapChainFn>(vtable[kFactoryCreateSwapChainIndex]),
        nullptr,
    };
    if (!originals.createSwapChain) return false;

    void* previousCreateSwapChain = nullptr;
    if (!ReplacePointer(&vtable[kFactoryCreateSwapChainIndex],
                        reinterpret_cast<void*>(&HookedFactoryCreateSwapChain), previousCreateSwapChain)) {
        return false;
    }

    ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2))) && factory2) {
        originals.createSwapChainForHwnd =
            reinterpret_cast<DxgiCreateSwapChainForHwndFn>(vtable[kFactory2CreateSwapChainForHwndIndex]);
        void* previousCreateSwapChainForHwnd = nullptr;
        if (originals.createSwapChainForHwnd &&
            !ReplacePointer(&vtable[kFactory2CreateSwapChainForHwndIndex],
                            reinterpret_cast<void*>(&HookedFactoryCreateSwapChainForHwnd),
                            previousCreateSwapChainForHwnd)) {
            originals.createSwapChainForHwnd = nullptr;
        }
    }

    g_factoryVtables.emplace(vtable, originals);
    return true;
}

PresentFn OriginalPresent(IDXGISwapChain* swapChain) {
    if (!swapChain) return nullptr;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_swapChainVtables.find(vtable);
    return found == g_swapChainVtables.end() ? nullptr : found->second.present;
}

ResizeBuffersFn OriginalResizeBuffers(IDXGISwapChain* swapChain) {
    if (!swapChain) return nullptr;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_swapChainVtables.find(vtable);
    return found == g_swapChainVtables.end() ? nullptr : found->second.resizeBuffers;
}

OMSetRenderTargetsFn OriginalOMSetRenderTargets(ID3D11DeviceContext* context) {
    if (!context) return nullptr;
    void** vtable = *reinterpret_cast<void***>(context);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_contextVtables.find(vtable);
    return found == g_contextVtables.end() ? nullptr : found->second.omSetRenderTargets;
}

DxgiCreateSwapChainFn OriginalFactoryCreateSwapChain(IDXGIFactory* factory) {
    if (!factory) return nullptr;
    void** vtable = *reinterpret_cast<void***>(factory);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_factoryVtables.find(vtable);
    return found == g_factoryVtables.end() ? nullptr : found->second.createSwapChain;
}

DxgiCreateSwapChainForHwndFn OriginalFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory) {
    if (!factory) return nullptr;
    void** vtable = *reinterpret_cast<void***>(factory);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_factoryVtables.find(vtable);
    return found == g_factoryVtables.end() ? nullptr : found->second.createSwapChainForHwnd;
}

// Resolves the ID3D11Device/ID3D11DeviceContext behind the IUnknown* that CreateSwapChain and
// CreateSwapChainForHwnd receive (the app's already-created device, since the split path never
// funnels device creation through the swap-chain call), then attaches the capture session exactly
// as the monolithic D3D11CreateDeviceAndSwapChain path does.
void AttachFromDeviceUnknown(IUnknown* deviceUnknown, IDXGISwapChain* swapChain) {
    if (!deviceUnknown || !swapChain) return;
    PatchSwapChainVtable(swapChain);
    ComPtr<ID3D11Device> device;
    if (FAILED(deviceUnknown->QueryInterface(IID_PPV_ARGS(&device))) || !device) return;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    if (context) PatchContextVtable(context.Get());
    g_captureSession.Attach(device.Get(), context.Get(), swapChain);
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                                 ID3D11RenderTargetView* const* renderTargetViews,
                                                 ID3D11DepthStencilView* depthStencilView) {
    g_captureSession.OnSetRenderTargets(numViews, renderTargetViews, depthStencilView);
    const OMSetRenderTargetsFn original = OriginalOMSetRenderTargets(context);
    if (original) original(context, numViews, renderTargetViews, depthStencilView);
}

