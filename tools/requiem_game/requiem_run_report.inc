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
        std::cout << "[REFLEX-REPORT] Reflex report indisponivel nesta execucao (latencyReportAvailable = false)." << std::endl;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        int slModuleCount = 0;
        std::vector<std::string> slNames;
        std::vector<std::string> slDirs;
        bool duplicateFound = false;
        std::cout << "[STREAMLINE-STACK]" << std::endl;
        if (Module32FirstW(snap, &me)) {
            do {
                if (wcsstr(me.szModule, L"sl.") != nullptr) {
                    slModuleCount++;
                    char modulePath[MAX_PATH]{};
                    WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, modulePath, sizeof(modulePath), nullptr, nullptr);
                    char moduleName[MAX_PATH]{};
                    WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, moduleName, sizeof(moduleName), nullptr, nullptr);
                    std::string n(moduleName);
                    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                    if (std::find(slNames.begin(), slNames.end(), n) != slNames.end()) duplicateFound = true;
                    slNames.push_back(n);

                    std::string p(modulePath);
                    std::string dir = p.substr(0, p.find_last_of("\\/"));
                    std::transform(dir.begin(), dir.end(), dir.begin(), ::tolower);
                    if (std::find(slDirs.begin(), slDirs.end(), dir) == slDirs.end()) slDirs.push_back(dir);

                    std::cout << "  module=" << moduleName << " base=0x" << std::hex << reinterpret_cast<uintptr_t>(me.modBaseAddr)
                              << std::dec << " path=" << modulePath << std::endl;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        const bool divergentDirs = slDirs.size() > 1;
        const bool duplicateStack = duplicateFound || divergentDirs;
        std::cout << "  modules_detected=" << slModuleCount
                  << " distinct_directories=" << slDirs.size()
                  << " duplicate_stack=" << (duplicateStack ? "YES" : "NO");
        if (duplicateStack) {
            if (duplicateFound) std::cout << " (duplicate module detected)";
            if (divergentDirs) std::cout << " (divergent directories detected)";
        }
        std::cout << std::endl;
    }
    std::cout << "[FRAME-LATENCY-OWNERSHIP] waitable_flag=" << (g_hasWaitableFlag ? "YES" : "NO")
              << " waitable_handle=" << (g_hasWaitableObject ? "VALID" : "NONE")
              << " max_latency=" << (g_effectiveMaxLatency > 0 ? std::to_string(g_effectiveMaxLatency) : "UNAVAILABLE")
              << " swapchain_flags=0x" << std::hex << g_swapChainFlags << std::dec
              << " owner=" << (g_hasWaitableFlag ? "NRFusion/Game" : "DXGI_DEFAULT")
              << std::endl;
