NrKernelAbiProbe& NrKernelAbiProbe::Instance() {
    static NrKernelAbiProbe probe;
    return probe;
}

bool NrKernelAbiProbe::Running() const noexcept {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.running;
}

bool NrKernelAbiProbe::Start(const std::string& filter) {
#if defined(_WIN32) && defined(NRFUSION_ABI_HAS_DETOURS)
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.running) return true;
    HMODULE driver = GetModuleHandleW(L"nvcuda.dll");
    if (!driver) driver = LoadLibraryW(L"nvcuda.dll");
    if (!driver) return false;
    auto symbol = [driver](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(driver, name));
    };
    state.original = reinterpret_cast<PFN_cuLaunchKernel>(symbol("cuLaunchKernel"));
    state.funcName = reinterpret_cast<PFN_cuFuncGetName>(symbol("cuFuncGetName"));
    state.paramInfo = reinterpret_cast<PFN_cuFuncGetParamInfo>(symbol("cuFuncGetParamInfo"));
    state.copyOut = reinterpret_cast<PFN_cuMemcpyDtoH>(symbol("cuMemcpyDtoH_v2"));
    state.pointerAttribute =
        reinterpret_cast<PFN_cuPointerGetAttribute>(symbol("cuPointerGetAttribute"));
    state.synchronize = reinterpret_cast<PFN_cuStreamSynchronize>(symbol("cuStreamSynchronize_v2"));
    if (!state.synchronize)
        state.synchronize = reinterpret_cast<PFN_cuStreamSynchronize>(symbol("cuStreamSynchronize"));
    // Without the parameter layout there is nothing to walk, and without the copy there is
    // nothing to compare. Either missing makes the answer a guess, so the probe refuses.
    if (!state.original || !state.funcName || !state.paramInfo || !state.copyOut ||
        !state.pointerAttribute)
        return false;

    state.filter = filter;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(state.original), reinterpret_cast<PVOID>(&Hooked));
    if (DetourTransactionCommit() != NO_ERROR) return false;
    state.running = true;
    return true;
#else
    (void) filter;
    (void) &Hooked;
    return false;
#endif
}

void NrKernelAbiProbe::Stop() {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.running) return;
    state.running = false;
#if defined(NRFUSION_ABI_HAS_DETOURS)
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(state.original), reinterpret_cast<PVOID>(&Hooked));
    DetourTransactionCommit();
#endif
}

void NrKernelAbiProbe::Reset() {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.records.clear();
}

std::vector<KernelAbi> NrKernelAbiProbe::Report() const {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    std::vector<KernelAbi> out;
    out.reserve(state.records.size());
    for (const auto& entry : state.records) {
        KernelAbi abi;
        abi.name = entry.second.name;
        abi.launches = entry.second.launches;
        for (std::size_t i = 0; i < entry.second.slots.size(); ++i) {
            const Slot& slot = entry.second.slots[i];
            abi.arguments.push_back({i, slot.size, slot.role, slot.value, slot.changedByLaunch,
                                     slot.changedBetween, slot.observations});
        }
        out.push_back(std::move(abi));
    }
    std::sort(out.begin(), out.end(),
              [](const KernelAbi& a, const KernelAbi& b) { return a.launches > b.launches; });
    return out;
}

std::string NrKernelAbiProbe::FormatReport() const {
    std::string text;
    char line[512];
    for (const KernelAbi& abi : Report()) {
        std::snprintf(line, sizeof(line), "\n%s  (%llu lancamentos)\n", abi.name.c_str(),
                      (unsigned long long) abi.launches);
        text += line;
        std::snprintf(line, sizeof(line), "  %-5s %-6s %-10s %-18s %s\n",
                      "arg", "bytes", "papel", "endereco", "mudou (pelo/entre)");
        text += line;
        for (const KernelArgument& argument : abi.arguments) {
            std::snprintf(line, sizeof(line), "  %-5zu %-6zu %-10s 0x%016llx  %llu / %llu\n",
                          argument.index, argument.sizeBytes, Describe(argument.role),
                          (unsigned long long) argument.value,
                          (unsigned long long) argument.timesChangedByLaunch,
                          (unsigned long long) argument.timesChangedBetweenLaunches);
            text += line;
        }
    }
    if (text.empty()) text = "nenhum lancamento observado ainda\n";
    return text;
}

