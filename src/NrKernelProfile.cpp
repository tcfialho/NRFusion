#include "nrfusion/NrKernelProfile.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
// Without these two, windows.h defines min and max as macros and every std::min in this file
// stops parsing. The Microsoft compiler is where that bites; it is silent elsewhere.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Detours ships with the host this patcher targets, not with the portable core. Where it is
// absent the profiler still compiles and simply reports that it cannot start, so the core
// never grows a dependency it does not need.
#if defined(_WIN32) && defined(__has_include)
#if __has_include(<detours.h>)
#include <detours.h>
#define NRFUSION_HAS_DETOURS 1
#endif
#endif

namespace nrfusion {
namespace {

// Only the handful of driver entry points this needs, declared locally so the project does
// not take a build dependency on the CUDA toolkit to carry the profiler.
using CUfunction = void*;
using CUstream = void*;
using CUevent = void*;
using CUresult = int;
constexpr CUresult CUDA_SUCCESS_ = 0;

using PFN_cuLaunchKernel = CUresult(*)(CUfunction, unsigned, unsigned, unsigned,
                                       unsigned, unsigned, unsigned, unsigned,
                                       CUstream, void**, void**);
using PFN_cuEventCreate = CUresult(*)(CUevent*, unsigned);
using PFN_cuEventRecord = CUresult(*)(CUevent, CUstream);
using PFN_cuEventQuery = CUresult(*)(CUevent);
using PFN_cuEventElapsedTime = CUresult(*)(float*, CUevent, CUevent);
using PFN_cuEventDestroy = CUresult(*)(CUevent);
using PFN_cuFuncGetName = CUresult(*)(const char**, CUfunction);

struct Driver {
    PFN_cuEventCreate create = nullptr;
    PFN_cuEventRecord record = nullptr;
    PFN_cuEventQuery query = nullptr;
    PFN_cuEventElapsedTime elapsed = nullptr;
    PFN_cuEventDestroy destroy = nullptr;
    PFN_cuFuncGetName funcName = nullptr;
    bool ready = false;
};

// A launch is timed by a pair of events recorded around it on the same stream. Resolving them
// immediately would mean waiting for the GPU, so pairs go into a ring and are read back only
// once the driver reports them complete. When the ring fills the sample is dropped rather
// than stalling the frame: a measurement is not worth a stutter.
struct Sample {
    CUevent start = nullptr;
    CUevent stop = nullptr;
    const char* name = nullptr;
    bool live = false;
};

struct State {
    std::mutex mutex;
    Driver driver;
    bool running = false;
    PFN_cuLaunchKernel original = nullptr;
    std::vector<Sample> ring;
    std::size_t cursor = 0;
    std::unordered_map<std::string, NrKernelStat> stats;
    std::uint64_t frames = 0;
    std::uint64_t dropped = 0;
};

State& Shared() {
    static State state;
    return state;
}

// The neural-rendering kernels the vendor runtime launches carry these markers in their
// names. Everything else in the process -- the game's own compute, other libraries -- is left
// untouched so the table describes the neural pass and nothing else.
bool IsNeuralKernel(const char* name) {
    if (!name) return false;
    static const char* markers[] = {"swin", "ffwd", "qkv", "attn", "dlssnr", "cc_", "conv_res"};
    for (const char* marker : markers) {
        for (const char* at = name; *at; ++at) {
            const char* a = at;
            const char* b = marker;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (!*b) return true;
        }
    }
    return false;
}

void Resolve(State& state) {
    for (auto& sample : state.ring) {
        if (!sample.live) continue;
        if (state.driver.query(sample.stop) != CUDA_SUCCESS_) continue;
        float milliseconds = 0.0f;
        if (state.driver.elapsed(&milliseconds, sample.start, sample.stop) == CUDA_SUCCESS_) {
            auto& entry = state.stats[sample.name ? sample.name : "?"];
            if (entry.calls == 0) {
                entry.name = sample.name ? sample.name : "?";
                entry.minMs = entry.maxMs = milliseconds;
            } else {
                entry.minMs = std::min(entry.minMs, static_cast<double>(milliseconds));
                entry.maxMs = std::max(entry.maxMs, static_cast<double>(milliseconds));
            }
            entry.calls += 1;
            entry.totalMs += milliseconds;
        }
        sample.live = false;
    }
}

CUresult Hooked(CUfunction function, unsigned gx, unsigned gy, unsigned gz,
                unsigned bx, unsigned by, unsigned bz, unsigned shared,
                CUstream stream, void** params, void** extra) {
    State& state = Shared();
    PFN_cuLaunchKernel original = nullptr;
    const char* name = nullptr;
    Sample* sample = nullptr;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        original = state.original;
        if (state.running && state.driver.ready) {
            if (state.driver.funcName) state.driver.funcName(&name, function);
            if (IsNeuralKernel(name)) {
                Sample& slot = state.ring[state.cursor];
                if (slot.live) {
                    state.dropped += 1;
                } else {
                    slot.name = name;
                    slot.live = true;
                    sample = &slot;
                    state.cursor = (state.cursor + 1) % state.ring.size();
                }
            }
        }
    }
    if (!original) return 1;
    if (sample) state.driver.record(sample->start, stream);
    const CUresult result = original(function, gx, gy, gz, bx, by, bz, shared, stream, params, extra);
    if (sample) state.driver.record(sample->stop, stream);
    return result;
}

} // namespace

NrKernelProfiler& NrKernelProfiler::Instance() {
    static NrKernelProfiler profiler;
    return profiler;
}

bool NrKernelProfiler::Running() const noexcept {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.running;
}

bool NrKernelProfiler::Start() {
#if defined(_WIN32)
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.running) return true;

    HMODULE driver = GetModuleHandleW(L"nvcuda.dll");
    if (!driver) driver = LoadLibraryW(L"nvcuda.dll");
    if (!driver) return false;

    auto symbol = [driver](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(driver, name));
    };
    state.driver.create = reinterpret_cast<PFN_cuEventCreate>(symbol("cuEventCreate"));
    state.driver.record = reinterpret_cast<PFN_cuEventRecord>(symbol("cuEventRecord"));
    state.driver.query = reinterpret_cast<PFN_cuEventQuery>(symbol("cuEventQuery"));
    state.driver.elapsed = reinterpret_cast<PFN_cuEventElapsedTime>(symbol("cuEventElapsedTime"));
    state.driver.destroy = reinterpret_cast<PFN_cuEventDestroy>(symbol("cuEventDestroy"));
    // Present since CUDA 12.3. Without it kernels cannot be told apart, and an unnamed table
    // is worthless, so the profiler refuses to start rather than produce one.
    state.driver.funcName = reinterpret_cast<PFN_cuFuncGetName>(symbol("cuFuncGetName"));
    state.original = reinterpret_cast<PFN_cuLaunchKernel>(symbol("cuLaunchKernel"));
    if (!state.driver.create || !state.driver.record || !state.driver.query ||
        !state.driver.elapsed || !state.driver.funcName || !state.original)
        return false;

    state.ring.assign(256, Sample{});
    for (auto& sample : state.ring) {
        if (state.driver.create(&sample.start, 0) != CUDA_SUCCESS_ ||
            state.driver.create(&sample.stop, 0) != CUDA_SUCCESS_)
            return false;
    }
#if defined(NRFUSION_HAS_DETOURS)
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&reinterpret_cast<PVOID&>(state.original), reinterpret_cast<PVOID>(&Hooked));
    const bool hooked = DetourTransactionCommit() == NO_ERROR;
#else
    // Without a detour the launches cannot be intercepted, and reporting an empty table as if
    // it were a measurement would be worse than refusing.
    (void) &Hooked;
    const bool hooked = false;
#endif
    if (!hooked) return false;

    state.driver.ready = true;
    state.cursor = 0;
    state.running = true;
    return true;
#else
    return false;
#endif
}

void NrKernelProfiler::Stop() {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.running) return;
    state.running = false;
#if defined(NRFUSION_HAS_DETOURS)
    // The detour is removed rather than left in place behind a flag: a hook on every launch
    // is not something to leave installed in a game that is no longer being measured.
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&reinterpret_cast<PVOID&>(state.original), reinterpret_cast<PVOID>(&Hooked));
    DetourTransactionCommit();
#endif
}

void NrKernelProfiler::MarkFrame() {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.running) return;
    state.frames += 1;
    Resolve(state);
}

void NrKernelProfiler::Reset() {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    state.stats.clear();
    state.frames = 0;
    state.dropped = 0;
}

NrKernelReport NrKernelProfiler::Report() const {
    State& state = Shared();
    std::lock_guard<std::mutex> guard(state.mutex);
    NrKernelReport report;
    report.frames = state.frames;
    report.droppedSamples = state.dropped;
    report.kernels.reserve(state.stats.size());
    for (const auto& entry : state.stats) report.kernels.push_back(entry.second);
    std::sort(report.kernels.begin(), report.kernels.end(),
              [](const NrKernelStat& a, const NrKernelStat& b) { return a.totalMs > b.totalMs; });
    double total = 0.0;
    for (const auto& kernel : report.kernels) total += kernel.totalMs;
    report.measuredMsPerFrame = state.frames ? total / static_cast<double>(state.frames) : 0.0;
    return report;
}

std::string NrKernelProfiler::FormatReport() const {
    const NrKernelReport report = Report();
    std::string text;
    char line[512];
    std::snprintf(line, sizeof(line),
                  "NRFusion: %llu quadros, %.3f ms/quadro somados nos kernels CUDA capturados (nao e o tempo do passe NR), %llu amostras perdidas\n",
                  (unsigned long long) report.frames, report.measuredMsPerFrame,
                  (unsigned long long) report.droppedSamples);
    text += line;
    std::snprintf(line, sizeof(line), "%-52s %8s %10s %10s %7s\n",
                  "kernel", "chamadas", "ms/quadro", "ms/chamada", "fatia");
    text += line;
    for (const auto& kernel : report.kernels) {
        const double perFrame = report.frames ? kernel.totalMs / (double) report.frames : 0.0;
        const double callsPerFrame = report.frames ? (double) kernel.calls / (double) report.frames : 0.0;
        std::snprintf(line, sizeof(line), "%-52s %8.1f %10.4f %10.4f %6.1f%%\n",
                      kernel.name.c_str(), callsPerFrame, perFrame, kernel.meanMs(),
                      report.shareOf(kernel) * 100.0);
        text += line;
    }
    return text;
}

} // namespace nrfusion
