    if (!nativeFrameIntervalSamples.empty()) {
        std::sort(nativeFrameIntervalSamples.begin(), nativeFrameIntervalSamples.end());
        std::sort(pacingSleepSamples.begin(), pacingSleepSamples.end());
        const size_t ni = nativeFrameIntervalSamples.size();
        const double i50 = nativeFrameIntervalSamples[ni * 50 / 100];
        const double i95 = nativeFrameIntervalSamples[ni * 95 / 100];
        const double nativeFps = (i50 > 0.0) ? (1000.0 / i50) : 0.0;
        const size_t ns = pacingSleepSamples.size();
        const double s50 = pacingSleepSamples[ns * 50 / 100];
        const double s95 = pacingSleepSamples[ns * 95 / 100];
        const double effectiveDisplayCadence = nativeFps * (g_mfgMultiplier > 0 ? g_mfgMultiplier : 1);
        std::cout << "[PACER-TIMING-REPORT] samples=" << ni
                  << " NativeFrameInterval p50=" << i50 << "ms p95=" << i95 << "ms"
                  << " -> NativeFPS=" << nativeFps
                  << " | PacingSleep p50=" << s50 << "ms p95=" << s95 << "ms"
                  << std::endl;
        std::cout << "[DISPLAY-CADENCE-REPORT] OSReportedRefresh="
                  << (g_osReportedRefresh > 0 ? std::to_string(g_osReportedRefresh) + " Hz" : "unknown")
                  << " | EffectiveObservedDisplayCadence=" << effectiveDisplayCadence << " Hz"
                  << " | RequestedTargetDisplayFps=" << (g_mfgTargetDisplayFps > 0 ? std::to_string(g_mfgTargetDisplayFps) + " Hz" : "default/unlimited")
                  << std::endl;
    }
    if (!postPacingCpuToPresentReturnSamples.empty()) {
        std::sort(postPacingCpuToPresentReturnSamples.begin(), postPacingCpuToPresentReturnSamples.end());
        std::sort(postPacingCpuToPresentStartSamples.begin(), postPacingCpuToPresentStartSamples.end());
        const size_t n = postPacingCpuToPresentReturnSamples.size();
        const double p50 = postPacingCpuToPresentReturnSamples[n * 50 / 100];
        const double p95 = postPacingCpuToPresentReturnSamples[n * 95 / 100];
        const double p99 = postPacingCpuToPresentReturnSamples[n * 99 / 100];
        const double startP50 = postPacingCpuToPresentStartSamples[n * 50 / 100];
        const double startP95 = postPacingCpuToPresentStartSamples[n * 95 / 100];
        const double startP99 = postPacingCpuToPresentStartSamples[n * 99 / 100];
        std::cout << "[CPU-PACING-REPORT] samples=" << n
                  << " PostPacingCpuToPresentReturn p50=" << p50 << "ms p95=" << p95 << "ms p99=" << p99 << "ms"
                  << " | PostPacingCpuToPresentStart p50=" << startP50 << "ms p95=" << startP95 << "ms p99=" << startP99 << "ms"
                  << std::endl;
    }
    using GetReflexOwnershipFn = int(__cdecl*)(nrfusion::ReflexOwnershipInfo*);
    using GetReflexStatsFn = int(__cdecl*)(nrfusion::ReflexValidationStats*);
    const auto getReflexOwnership = standaloneProxy ? reinterpret_cast<GetReflexOwnershipFn>(
        GetProcAddress(standaloneProxy, "NRFusion_GetReflexOwnership")) : nullptr;
    const auto getReflexStats = standaloneProxy ? reinterpret_cast<GetReflexStatsFn>(
        GetProcAddress(standaloneProxy, "NRFusion_GetReflexStats")) : nullptr;

    nrfusion::ReflexOwnershipInfo reflexOwnership{};
    nrfusion::ReflexValidationStats reflexStats{};
    if (getReflexOwnership) getReflexOwnership(&reflexOwnership);
    if (getReflexStats) getReflexStats(&reflexStats);
    if (reflexOwnership.haveGameOptions) {
        std::cout << "[REFLEX-OWNERSHIP] gameMode=" << reflexOwnership.gameMode
                  << " frameLimitUs=" << reflexOwnership.gameFrameLimitUs
                  << " markersOpt=" << (reflexOwnership.gameUseMarkersToOptimize ? 1 : 0)
                  << " vk=" << reflexOwnership.gameVirtualKey
                  << " thread=" << reflexOwnership.gameIdThread
                  << " effectiveMode=" << reflexOwnership.effectiveMode
                  << " promoted=" << (reflexOwnership.mfgPromotedReflex ? 1 : 0)
                  << " latencyMode=" << reflexOwnership.latencyMode
                  << " targetDisplayFps=" << reflexOwnership.targetDisplayFps
                  << " targetNativeFps=" << reflexOwnership.targetNativeFps
                  << " effectiveFrameLimitUs=" << reflexOwnership.effectiveFrameLimitUs
                  << std::endl;
    }
    std::cout << "[REFLEX-VALIDATION] frames=" << reflexStats.framesAnalyzed
              << " missingSleep=" << reflexStats.missingSleeps
              << " duplicateSleep=" << reflexStats.duplicateSleeps
              << " mixedTokens=" << reflexStats.mixedTokens
              << " orderViolations=" << reflexStats.orderViolations
              << " missingMarkers=" << reflexStats.missingMarkerFrames
              << " perfectFrames=" << reflexStats.perfectFrames
              << std::endl;

    const auto reflexLatency = mfg.GetReflexLatencyStats();
    if (reflexLatency.available && reflexLatency.samples > 0) {
        std::cout << "[REFLEX-REPORT] samples=" << reflexLatency.samples << std::endl;
        std::cout << "  InputSample->SimStart: p50=" << reflexLatency.inputToSimStart.p50
                  << "ms p95=" << reflexLatency.inputToSimStart.p95
                  << "ms p99=" << reflexLatency.inputToSimStart.p99 << "ms" << std::endl;
        std::cout << "  InputSample->RenderSubmitStart: p50=" << reflexLatency.inputToRenderSubmitStart.p50
                  << "ms p95=" << reflexLatency.inputToRenderSubmitStart.p95
                  << "ms p99=" << reflexLatency.inputToRenderSubmitStart.p99 << "ms" << std::endl;
        std::cout << "  InputSample->PresentStart: p50=" << reflexLatency.inputToPresentStart.p50
                  << "ms p95=" << reflexLatency.inputToPresentStart.p95
                  << "ms p99=" << reflexLatency.inputToPresentStart.p99 << "ms" << std::endl;
        std::cout << "  InputSample->PresentEnd: p50=" << reflexLatency.inputToPresentEnd.p50
                  << "ms p95=" << reflexLatency.inputToPresentEnd.p95
                  << "ms p99=" << reflexLatency.inputToPresentEnd.p99 << "ms" << std::endl;
        std::cout << "  InputSample->DriverEnd: p50=" << reflexLatency.inputToDriverEnd.p50
                  << "ms p95=" << reflexLatency.inputToDriverEnd.p95
                  << "ms p99=" << reflexLatency.inputToDriverEnd.p99 << "ms" << std::endl;
        std::cout << "  InputSample->OSQueueEnd: p50=" << reflexLatency.inputToOsQueueEnd.p50
                  << "ms p95=" << reflexLatency.inputToOsQueueEnd.p95
                  << "ms p99=" << reflexLatency.inputToOsQueueEnd.p99 << "ms" << std::endl;
        std::cout << "  InputSample->GPURenderEnd: p50=" << reflexLatency.inputToGpuRenderEnd.p50
                  << "ms p95=" << reflexLatency.inputToGpuRenderEnd.p95
                  << "ms p99=" << reflexLatency.inputToGpuRenderEnd.p99 << "ms" << std::endl;
    } else {
        std::cout << "[REFLEX-REPORT] Reflex report indisponivel nesta execucao (latencyReportAvailable = false; root cause = UNKNOWN)." << std::endl;
    }

    auto QueryFileVersion = [](const wchar_t* path) -> std::string {
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(path, &handle);
        if (size == 0) return "UNKNOWN";
        std::vector<BYTE> data(size);
        if (!GetFileVersionInfoW(path, 0, size, data.data())) return "UNKNOWN";
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT len = 0;
        if (VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) && ffi) {
            return std::to_string(HIWORD(ffi->dwFileVersionMS)) + "." +
                   std::to_string(LOWORD(ffi->dwFileVersionMS)) + "." +
                   std::to_string(HIWORD(ffi->dwFileVersionLS)) + "." +
                   std::to_string(LOWORD(ffi->dwFileVersionLS));
        }
        return "UNKNOWN";
    };

    struct ExpectedModule {
        const char* key;
        const wchar_t* filePattern;
        const wchar_t* otaPattern;
        bool loaded = false;
        std::string fullPath;
        uintptr_t base = 0;
        std::string version = "UNKNOWN";
    };
    ExpectedModule expectedStack[] = {
        {"sl.interposer.dll", L"sl.interposer.dll", nullptr},
        {"sl.common.dll",     L"sl.common.dll",     L"sl_common_override"},
        {"sl.reflex.dll",     L"sl.reflex.dll",     L"sl_reflex_override"},
        {"sl.pcl.dll",        L"sl.pcl.dll",        L"sl_pcl_override"},
        {"sl.dlss_g.dll",     L"sl.dlss_g.dll",     L"sl_dlss_g_override"},
    };

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    bool otaOverrideActive = false;
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            do {
                char pathUtf8[MAX_PATH]{};
                WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);
                for (auto& exp : expectedStack) {
                    bool match = (wcsstr(me.szModule, exp.filePattern) != nullptr);
                    if (!match && exp.otaPattern && wcsstr(me.szExePath, exp.otaPattern) != nullptr) {
                        match = true;
                        otaOverrideActive = true;
                    }
                    if (match && !exp.loaded) {
                        exp.loaded = true;
                        exp.fullPath = pathUtf8;
                        exp.base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                        exp.version = QueryFileVersion(me.szExePath);
                    }
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
    }

    std::cout << "[STREAMLINE-STACK]" << std::endl;
    int loadedCount = 0;
    for (const auto& exp : expectedStack) {
        if (exp.loaded) loadedCount++;
        std::cout << "  " << exp.key << ": loaded=" << (exp.loaded ? "YES" : "NO");
        if (exp.loaded) {
            std::cout << " base=0x" << std::hex << exp.base << std::dec
                      << " ver=" << exp.version
                      << " path=" << exp.fullPath;
        }
        std::cout << std::endl;
    }
    if (otaOverrideActive) {
        std::cout << "  [NOTE] NVIDIA NGX DRS key SL_DLSS_OVERRIDE active: plugins loaded via OTA model cache" << std::endl;
    }
    std::cout << "  stack_status=" << (loadedCount == 5 ? "COMPLETE_QUALIFIED" : "PARTIAL")
              << " (" << loadedCount << "/5 modules verified)" << std::endl;

    std::cout << "[FRAME-LATENCY-OWNERSHIP] clientSwapchain=" << (g_isProxySwapchain ? "PROXY" : "NATIVE")
              << " nativeSwapchain=0x" << std::hex << reinterpret_cast<uintptr_t>(g_nativeSwapChain)
              << " proxySwapchain=0x" << reinterpret_cast<uintptr_t>(g_proxySwapChain) << std::dec
              << " clientRequestedWaitableFlag=" << (g_clientRequestedWaitableFlag ? "YES" : "NO")
              << " waitable_handle=" << (g_hasWaitableObject ? "VALID" : "NONE")
              << " max_latency=" << (g_effectiveMaxLatency > 0 ? std::to_string(g_effectiveMaxLatency) : "UNAVAILABLE")
              << " swapchain_flags=0x" << std::hex << g_swapChainFlags << std::dec
              << " owner=" << g_waitableOwner
              << " steady_state_wait=NONE"
              << std::endl;
