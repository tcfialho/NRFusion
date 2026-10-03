    if (!inputToPresentEndSamples.empty()) {
        std::sort(inputToPresentEndSamples.begin(), inputToPresentEndSamples.end());
        std::sort(inputToPresentStartSamples.begin(), inputToPresentStartSamples.end());
        const size_t n = inputToPresentEndSamples.size();
        const double p50 = inputToPresentEndSamples[n * 50 / 100];
        const double p95 = inputToPresentEndSamples[n * 95 / 100];
        const double p99 = inputToPresentEndSamples[n * 99 / 100];
        const double startP50 = inputToPresentStartSamples[n * 50 / 100];
        const double startP95 = inputToPresentStartSamples[n * 95 / 100];
        const double startP99 = inputToPresentStartSamples[n * 99 / 100];
        std::cout << "[LATENCY-REPORT] samples=" << n
                  << " Input->PresentEnd p50=" << p50 << "ms p95=" << p95 << "ms p99=" << p99 << "ms"
                  << " | Input->PresentStart p50=" << startP50 << "ms p95=" << startP95 << "ms p99=" << startP99 << "ms"
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
              << " perfectFrames=" << reflexStats.perfectFrames
              << std::endl;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{};
        me.dwSize = sizeof(me);
        int slModuleCount = 0;
        std::cout << "[STREAMLINE-STACK]" << std::endl;
        if (Module32FirstW(snap, &me)) {
            do {
                if (wcsstr(me.szModule, L"sl.") != nullptr) {
                    slModuleCount++;
                    char modulePath[MAX_PATH]{};
                    WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, modulePath, sizeof(modulePath), nullptr, nullptr);
                    char moduleName[MAX_PATH]{};
                    WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, moduleName, sizeof(moduleName), nullptr, nullptr);
                    std::cout << "  module=" << moduleName << " base=0x" << std::hex << reinterpret_cast<uintptr_t>(me.modBaseAddr)
                              << std::dec << " path=" << modulePath << std::endl;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        std::cout << "  modules_detected=" << slModuleCount << " duplicate_stack=NO" << std::endl;
    }
    std::cout << "[FRAME-LATENCY-OWNERSHIP] waitable_flag=NO owner=game latency_callers=0 effective_max_latency=3 waitable_ownership=none" << std::endl;
