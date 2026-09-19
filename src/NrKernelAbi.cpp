#include "nrfusion/NrKernelAbi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_WIN32) && defined(__has_include)
#if __has_include(<detours.h>)
#include <detours.h>
#define NRFUSION_ABI_HAS_DETOURS 1
#endif
#endif

namespace nrfusion {
namespace {

using CUfunction = void*;
using CUstream = void*;
using CUresult = int;
using CUdeviceptr = unsigned long long;
constexpr CUresult kOk = 0;
constexpr int kAttributeMemoryType = 2;   // CU_POINTER_ATTRIBUTE_MEMORY_TYPE
constexpr unsigned kMemoryTypeDevice = 2; // CU_MEMORYTYPE_DEVICE

// Enough bytes to tell buffers apart without paying for a real copy. A weight that differs from
// an input only past the first kilobyte would be indistinguishable, which is why the report says
// how many observations back each verdict.
constexpr std::size_t kSampleBytes = 512;

using PFN_cuLaunchKernel = CUresult(*)(CUfunction, unsigned, unsigned, unsigned,
                                       unsigned, unsigned, unsigned, unsigned,
                                       CUstream, void**, void**);
using PFN_cuFuncGetName = CUresult(*)(const char**, CUfunction);
using PFN_cuFuncGetParamInfo = CUresult(*)(CUfunction, std::size_t, std::size_t*, std::size_t*);
using PFN_cuMemcpyDtoH = CUresult(*)(void*, CUdeviceptr, std::size_t);
using PFN_cuPointerGetAttribute = CUresult(*)(void*, int, CUdeviceptr);
using PFN_cuStreamSynchronize = CUresult(*)(CUstream);

struct Slot {
    ArgumentRole role = ArgumentRole::Unknown;
    std::uint64_t value = 0;
    std::size_t size = 0;
    std::uint64_t changedByLaunch = 0;
    std::uint64_t changedBetween = 0;
    std::uint64_t observations = 0;
    bool haveSample = false;
    unsigned char lastAfter[kSampleBytes]{};
};

struct Record {
    std::string name;
    std::uint64_t launches = 0;
    std::vector<Slot> slots;
};

struct State {
    std::mutex mutex;
    bool running = false;
    std::string filter;
    PFN_cuLaunchKernel original = nullptr;
    PFN_cuFuncGetName funcName = nullptr;
    PFN_cuFuncGetParamInfo paramInfo = nullptr;
    PFN_cuMemcpyDtoH copyOut = nullptr;
    PFN_cuPointerGetAttribute pointerAttribute = nullptr;
    PFN_cuStreamSynchronize synchronize = nullptr;
    std::unordered_map<std::string, Record> records;
};

State& Shared() {
    static State state;
    return state;
}

bool IsDevicePointer(State& state, std::uint64_t value) {
    if (!value || !state.pointerAttribute) return false;
    unsigned type = 0;
    return state.pointerAttribute(&type, kAttributeMemoryType, value) == kOk &&
           type == kMemoryTypeDevice;
}

bool Sample(State& state, std::uint64_t address, unsigned char* out) {
    return state.copyOut && state.copyOut(out, address, kSampleBytes) == kOk;
}

// A role only moves in the direction the evidence allows. An argument seen written to once is an
// output forever, because a weight is never written; an argument that changes between launches
// can never go back to being a weight.
void Classify(Slot& slot, bool changedByLaunch, bool changedBetween) {
    if (changedByLaunch) {
        slot.changedByLaunch += 1;
        slot.role = ArgumentRole::Output;
        return;
    }
    if (changedBetween) {
        slot.changedBetween += 1;
        if (slot.role != ArgumentRole::Output) slot.role = ArgumentRole::Input;
        return;
    }
    if (slot.role == ArgumentRole::Unknown) slot.role = ArgumentRole::Weight;
}

CUresult Hooked(CUfunction function, unsigned gx, unsigned gy, unsigned gz,
                unsigned bx, unsigned by, unsigned bz, unsigned shared,
                CUstream stream, void** params, void** extra) {
    State& state = Shared();
    PFN_cuLaunchKernel original = nullptr;
    bool watch = false;
    const char* name = nullptr;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        original = state.original;
        if (state.running && params && state.funcName &&
            state.funcName(&name, function) == kOk && name)
            watch = state.filter.empty() || std::string(name).find(state.filter) != std::string::npos;
    }
    if (!original) return 1;
    if (!watch) return original(function, gx, gy, gz, bx, by, bz, shared, stream, params, extra);

    // Walk the declared parameters. Asking the driver rather than assuming a count is what keeps
    // this from reading past the end of someone else's argument array.
    struct Before { std::size_t index; std::size_t size; std::uint64_t value;
                    bool device; unsigned char bytes[kSampleBytes]; };
    std::vector<Before> before;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        for (std::size_t index = 0; state.paramInfo; ++index) {
            std::size_t offset = 0, size = 0;
            if (state.paramInfo(function, index, &offset, &size) != kOk) break;
            Before entry{index, size, 0, false, {}};
            if (size == sizeof(std::uint64_t) && params[index])
                std::memcpy(&entry.value, params[index], sizeof(entry.value));
            else if (size == sizeof(std::uint32_t) && params[index]) {
                std::uint32_t narrow = 0;
                std::memcpy(&narrow, params[index], sizeof(narrow));
                entry.value = narrow;
            }
            entry.device = IsDevicePointer(state, entry.value);
            if (entry.device) Sample(state, entry.value, entry.bytes);
            before.push_back(entry);
        }
    }

    const CUresult result = original(function, gx, gy, gz, bx, by, bz, shared, stream, params, extra);

    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (state.synchronize) state.synchronize(stream);
        Record& record = state.records[name];
        if (record.name.empty()) record.name = name;
        record.launches += 1;
        if (record.slots.size() < before.size()) record.slots.resize(before.size());
        for (const Before& entry : before) {
            Slot& slot = record.slots[entry.index];
            slot.size = entry.size;
            slot.value = entry.value;
            slot.observations += 1;
            if (!entry.device) {
                slot.role = ArgumentRole::Scalar;
                continue;
            }
            unsigned char after[kSampleBytes]{};
            if (!Sample(state, entry.value, after)) continue;
            const bool changedByLaunch = std::memcmp(entry.bytes, after, kSampleBytes) != 0;
            const bool changedBetween =
                slot.haveSample && std::memcmp(slot.lastAfter, entry.bytes, kSampleBytes) != 0;
            Classify(slot, changedByLaunch, changedBetween);
            std::memcpy(slot.lastAfter, after, kSampleBytes);
            slot.haveSample = true;
        }
    }
    return result;
}

} // namespace

const char* Describe(ArgumentRole role) noexcept {
    switch (role) {
    case ArgumentRole::Output: return "saida";
    case ArgumentRole::Weight: return "peso";
    case ArgumentRole::Input: return "entrada";
    case ArgumentRole::Scalar: return "escalar";
    case ArgumentRole::Unknown: break;
    }
    return "indefinido";
}

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

} // namespace nrfusion
