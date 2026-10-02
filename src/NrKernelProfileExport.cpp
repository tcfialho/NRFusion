#include "NrKernelProfileD3D12.hpp"
#include "NrKernelProfileStatistics.hpp"
#include "NrKernelRuntimeIdentity.hpp"

#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace nrfusion::kernelprofile {
namespace {

struct ExportState {
    std::mutex mutex;
    std::string path;
    std::ofstream csv;
};

ExportState& Export() {
    static ExportState state;
    return state;
}

void WriteName(std::ostream& output, const char* name) {
    output << '"';
    for (; *name; ++name) {
        if (*name == '"') output << '"';
        output << *name;
    }
    output << '"';
}

} // namespace

bool WriteFrame(const char* path, const LaunchRecord* records, std::size_t count,
                 const std::uint64_t* ticks, std::uint64_t frequency,
                 std::uintptr_t queue, std::uint64_t dropped) {
    auto& state = Export();
    AggregateFrame(records, count, ticks, frequency, queue, dropped);
    std::lock_guard lock(state.mutex);
    if (state.path != path) {
        state.csv.close();
        state.csv.open(path);
        if (!state.csv) return false;
        if (!WriteRuntimeIdentity(path, reinterpret_cast<void*>(queue))) return false;
        state.path = path;
        state.csv << "schema,frame,sequence,backend,function_identity,module_identity,generation,"
            "device_identity,command_list_identity,queue_identity,name,name_truncated,"
            "grid_x,grid_y,grid_z,block_x,block_y,block_z,shared_bytes,param_bytes,"
            "chain_id,chain_count,chain_index,gpu_ms,chain_gpu_ms,success,droppedSamples,module_sha256,custom\n";
    }
    auto& csv = state.csv;
    csv << std::setprecision(12);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& record = records[index];
        const auto& identity = record.identity;
        const bool valid = record.successful && ticks[record.query + 1] > ticks[record.query];
        const double ms = valid
            ? double(ticks[record.query + 1] - ticks[record.query]) * 1000.0 / double(frequency) : 0.0;
        csv << "1," << record.frame << ',' << record.sequence << ",nvapi_d3d12,"
            << identity.function << ',' << identity.module << ',' << identity.generation << ','
            << identity.device << ',' << record.commands << ',' << queue << ',';
        WriteName(csv, identity.name.data());
        csv << ',' << identity.truncated;
        for (unsigned dimension : record.grid) csv << ',' << dimension;
        for (unsigned dimension : record.block) csv << ',' << dimension;
        csv << ',' << record.sharedBytes << ',' << record.parameterBytes << ','
            << record.query / 2 << ',' << record.chainCount << ',' << record.chainIndex << ',';
        if (valid && record.chainCount == 1) csv << ms;
        csv << ',';
        if (valid) csv << ms;
        csv << ',' << record.successful << ',' << dropped << ',' << identity.moduleHash.data() << ',' << record.custom << '\n';
    }
    csv.flush();
    return static_cast<bool>(csv);
}

} // namespace nrfusion::kernelprofile
