#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelProfileD3D12.hpp"
#include "NrKernelProfileStatistics.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

using namespace nrfusion::kernelprofile;

LaunchRecord Projection(unsigned grid, unsigned query) {
    LaunchRecord record{};
    record.identity.function = 1;
    record.identity.module = 2;
    record.identity.device = 3;
    record.identity.generation = 4;
    std::memcpy(record.identity.name.data(), "projection", sizeof("projection"));
    record.grid = {grid, 1, 1};
    record.block = {128, 1, 1};
    record.query = query;
    record.chainCount = 1;
    record.successful = true;
    return record;
}

int main() {
    ResetNativeReport();
    const std::uint64_t ticks[]{1, 1001, 1, 3001};
    const LaunchRecord shapes[]{Projection(8, 0), Projection(16, 2)};
    AggregateFrame(shapes, 2, ticks, 1000000, 5, 0);
    auto report = NativeReport();
    assert(report.frames == 1 && report.kernels.size() == 2);
    assert(report.kernels[0].name == report.kernels[1].name);
    assert(report.kernels[0].shape.gridX != report.kernels[1].shape.gridX);
    assert(report.kernels[0].functionId == 1 && report.kernels[0].moduleId == 2);
    assert(report.kernels[0].generation == 4 && report.kernels[0].queueId == 5);
    assert(std::abs(report.measuredMsPerFrame - 4.0) < 1e-9);

    ResetNativeReport();
    LaunchRecord chain[]{Projection(8, 0), Projection(16, 2), Projection(32, 2)};
    chain[1].chainCount = chain[2].chainCount = 2;
    chain[2].chainIndex = 1;
    AggregateFrame(chain, 3, ticks, 1000000, 5, 0);
    report = NativeReport();
    assert(report.kernels.size() == 1);
    assert(std::abs(report.measuredMsPerFrame - 4.0) < 1e-9);
    assert(std::abs(report.unattributedMsPerFrame - 3.0) < 1e-9);
    assert(std::abs(report.shareOf(report.kernels[0]) - .25) < 1e-9);
    assert(report.droppedSamples == 0);

    ResetNativeReport();
    auto failed = Projection(8, 0);
    failed.successful = false;
    AggregateFrame(&failed, 1, ticks, 1000000, 5, 7);
    report = NativeReport();
    assert(report.droppedSamples == 8 && report.kernels.empty());
}
