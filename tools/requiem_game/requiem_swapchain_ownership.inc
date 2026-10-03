// Detect proxy vs native swapchain and classify frame latency ownership.
static const GUID kIID_StreamlineRetrieveBase = {
    0xADEC44E2, 0x61F0, 0x45C3, { 0xAD, 0x9F, 0x1B, 0x37, 0x37, 0x92, 0x84, 0xFF }
};
static const GUID kIID_StreamlineNative = {
    0xC2D1F762, 0xAC8B, 0x4DC7, { 0x97, 0xB2, 0xC7, 0xBC, 0xF4, 0xEB, 0x4C, 0x96 }
};
static const GUID kIID_StreamlineProxySwapchain = {
    0xD3F0BBFF, 0x3091, 0x4074, { 0x9D, 0x9E, 0xB9, 0x9C, 0xE2, 0xE5, 0xCF, 0x9A }
};

g_swapChainFlags = swapDesc.Flags;
g_clientRequestedWaitableFlag = (swapDesc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) != 0;
g_hasWaitableFlag = g_clientRequestedWaitableFlag;

void* nativeSc = nullptr;
if (SUCCEEDED(swapChain->QueryInterface(kIID_StreamlineRetrieveBase, &nativeSc)) && nativeSc) {
    g_isProxySwapchain = true;
    g_proxySwapChain = swapChain.Get();
    g_nativeSwapChain = nativeSc;
    reinterpret_cast<IUnknown*>(nativeSc)->Release();
} else if (SUCCEEDED(swapChain->QueryInterface(kIID_StreamlineNative, &nativeSc)) && nativeSc) {
    g_isProxySwapchain = true;
    g_proxySwapChain = swapChain.Get();
    g_nativeSwapChain = nativeSc;
    reinterpret_cast<IUnknown*>(nativeSc)->Release();
} else {
    void* proxyObj = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(kIID_StreamlineProxySwapchain, &proxyObj)) && proxyObj) {
        g_isProxySwapchain = true;
        g_proxySwapChain = swapChain.Get();
        g_nativeSwapChain = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(proxyObj) + 16);
        reinterpret_cast<IUnknown*>(proxyObj)->Release();
    } else {
        g_isProxySwapchain = false;
        g_proxySwapChain = nullptr;
        g_nativeSwapChain = swapChain.Get();
    }
}

if (!g_clientRequestedWaitableFlag && (g_isProxySwapchain || mfg.Requested())) {
    g_waitableOwner = "STREAMLINE_SL_PACER";
} else if (g_clientRequestedWaitableFlag) {
    g_waitableOwner = "CLIENT_APPLICATION";
} else {
    g_waitableOwner = "DXGI_DEFAULT";
}

if (g_waitableOwner != "STREAMLINE_SL_PACER") {
    ComPtr<IDXGISwapChain2> swapChain2;
    if (SUCCEEDED(swapChain.As(&swapChain2))) {
        HANDLE hWait = swapChain2->GetFrameLatencyWaitableObject();
        g_hasWaitableObject = (hWait != nullptr);
        UINT maxLat = 0;
        if (SUCCEEDED(swapChain2->GetMaximumFrameLatency(&maxLat))) {
            g_effectiveMaxLatency = maxLat;
        }
    }
} else {
    // Streamline DLSS-G pacer owns the waitable object; client application must not query or wait on it
    g_hasWaitableObject = false;
    g_effectiveMaxLatency = 1;
}
