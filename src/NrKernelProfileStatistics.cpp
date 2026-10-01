#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelProfileStatistics.hpp"
#include "NrKernelProfileD3D12.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>

namespace nrfusion::kernelprofile {
namespace {

using IdentityKey = std::tuple<std::uintptr_t, std::uintptr_t, std::uint64_t, std::uintptr_t,
    std::uintptr_t, std::string, std::array<std::uint32_t, 3>, std::array<std::uint32_t, 3>, std::uint32_t>;
struct StatisticsState {
    std::mutex mutex;
    std::map<IdentityKey, NrKernelStat> kernels;
    std::uint64_t frames = 0, dropped = 0;
    double totalMs = 0, unattributedMs = 0;
};

StatisticsState& Statistics() {
    static StatisticsState state;
    return state;
}

} // namespace

void AggregateFrame(const LaunchRecord* records, std::size_t count,
                    const std::uint64_t* ticks, std::uint64_t frequency,
                    std::uintptr_t queue, std::uint64_t dropped) {
    auto& state = Statistics();
    std::lock_guard lock(state.mutex);
    ++state.frames;
    state.dropped += dropped;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& record = records[index];
        if (!record.successful ||
            ticks[record.query + 1] <= ticks[record.query]) {
            ++state.dropped;
            continue;
        }
        const auto& identity = record.identity;
        const double ms = double(ticks[record.query + 1] - ticks[record.query]) * 1000.0 / double(frequency);
        if (record.chainIndex == 0) state.totalMs += ms;
        if (record.chainCount != 1) {
            if (record.chainIndex == 0) state.unattributedMs += ms;
            continue;
        }
        const IdentityKey key{identity.function, identity.module, identity.generation, identity.device,
            queue, identity.name.data(), record.grid, record.block, record.sharedBytes};
        auto& entry = state.kernels[key];
        if (!entry.calls) {
            entry.name = identity.name.data();
            entry.backend = "nvapi_d3d12";
            entry.moduleHash = identity.moduleHash.data();
            entry.functionId = identity.function;
            entry.moduleId = identity.module;
            entry.deviceId = identity.device;
            entry.queueId = queue;
            entry.generation = identity.generation;
            entry.shape = {record.grid[0], record.grid[1], record.grid[2],
                record.block[0], record.block[1], record.block[2], record.sharedBytes};
            entry.minMs = entry.maxMs = ms;
        }
        ++entry.calls;
        entry.totalMs += ms;
        entry.minMs = std::min(entry.minMs, ms);
        entry.maxMs = std::max(entry.maxMs, ms);
    }
}

NrKernelReport NativeReport() {
    auto& state = Statistics();
    std::lock_guard lock(state.mutex);
    NrKernelReport report{};
    report.frames = state.frames;
    report.droppedSamples = state.dropped;
    for (const auto& [identity, stat] : state.kernels) {
        report.kernels.push_back(stat);
    }
    std::sort(report.kernels.begin(), report.kernels.end(), [](const auto& first, const auto& second) {
        return first.totalMs > second.totalMs;
    });
    if (report.frames) {
        report.measuredMsPerFrame = state.totalMs / double(report.frames);
        report.unattributedMsPerFrame = state.unattributedMs / double(report.frames);
    }
    return report;
}

void ResetNativeReport() {
    auto& state = Statistics();
    std::lock_guard lock(state.mutex);
    state.kernels.clear();
    state.frames = state.dropped = 0;
    state.totalMs = state.unattributedMs = 0;
}

} // namespace nrfusion::kernelprofile
