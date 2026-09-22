#include "nrfusion/NrSession.hpp"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>

using namespace nrfusion;

namespace {

std::atomic<std::size_t> gAllocations{0};
std::atomic<bool> gMeasure{false};

struct FakeExecutor {
    bool Execute(NrSession& session, const NrSessionFrameResult& frame) noexcept {
        const auto work = session.BeginWork(frame);
        if (!work || !session.SubmitWork(*work) || !session.MapTimedWork(*work))
            return false;
        return session.RetireTimedInterval(2.0);
    }
};

NrSessionFramePacket Packet(std::uint64_t generation, FrameId frameId) {
    NrSessionFramePacket packet{};
    packet.game.api = GraphicsApi::D3D12;
    packet.frame.frameId = frameId;
    packet.frame.configurationGeneration = generation;
    packet.frame.api = GraphicsApi::D3D12;
    packet.frame.renderResolution = {1920, 1080};
    packet.frame.outputResolution = {1920, 1080};
    packet.frame.color.opaqueId = 1;
    packet.frame.color.resolution = packet.frame.renderResolution;
    packet.frame.color.format = ResourceFormat::Rgba16Float;
    packet.capabilities.syntheticD3D12 = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    packet.telemetry.nrGpuMs = 2.0;
    packet.telemetry.frameGpuMs = 8.0;
    packet.telemetry.sourceFps = 60.0;
    packet.telemetry.processedFps = 60.0;
    return packet;
}

bool Step(NrSession& session, FakeExecutor& executor,
          NrSessionFramePacket& packet) noexcept {
    const NrSessionFrameResult result = session.Resolve(packet);
    if (!result || !executor.Execute(session, result)) return false;
    ++packet.frame.frameId;
    return true;
}

} // namespace

void* operator new(std::size_t size) {
    if (gMeasure.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main() {
    RuntimeConfig config{};
    config.generation = 1;
    config.enabled = true;
    config.targetFps = 60.0f;

    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    NrSession session;
    assert(session.Configure(config, performance));

    FakeExecutor executor;
    auto packet = Packet(config.generation, 1);

    for (std::size_t i = 0; i < 512; ++i)
        assert(Step(session, executor, packet));

    gAllocations.store(0, std::memory_order_relaxed);
    gMeasure.store(true, std::memory_order_relaxed);
    for (std::size_t i = 0; i < 1'000'000; ++i)
        assert(Step(session, executor, packet));
    gMeasure.store(false, std::memory_order_relaxed);

    assert(gAllocations.load(std::memory_order_relaxed) == 0);
    return 0;
}
