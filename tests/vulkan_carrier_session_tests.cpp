#include "nrfusion/VulkanCarrierSession.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

VulkanNativeImageFacts Image(
    std::uint64_t id,
    std::uint32_t usage) {
    VulkanNativeImageFacts image{};
    image.opaqueId = id;
    image.resolution = {1920, 1080};
    image.format = ResourceFormat::Rgba16Float;
    image.usage = usage;
    image.layout = VulkanImageLayoutIntent::General;
    image.queueFamilyIndex = 2;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.sampleCount = 1;
    image.image2D = true;
    return image;
}

VulkanCarrierFramePacket Packet(
    std::uint64_t generation,
    FrameId frameId) {
    VulkanCarrierFramePacket packet{};
    packet.game.api = GraphicsApi::Vulkan;
    packet.acquire.identity.frameId = frameId;
    packet.acquire.identity.viewId = 11;
    packet.acquire.identity.configurationGeneration = generation;
    packet.acquire.resourceGeneration = frameId;
    packet.acquire.color.image = Image(
        0x1000 + frameId,
        VulkanUsageSampled | VulkanUsageTransferSource);
    packet.acquire.color.provenance =
        ResourceProvenance::GameNative;
    packet.acquire.color.reliability =
        ResourceReliability::Reliable;
    packet.acquire.output = Image(
        0x2000 + frameId,
        VulkanUsageStorage | VulkanUsageTransferDestination);
    packet.capabilities.syntheticVulkan = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    packet.telemetry.nrGpuMs = 2.0;
    packet.telemetry.frameGpuMs = 8.0;
    packet.telemetry.sourceFps = 60.0;
    packet.telemetry.processedFps = 60.0;
    return packet;
}

} // namespace

int main() {
    RuntimeConfig config{};
    config.generation = 7;
    config.enabled = true;
    config.targetFps = 60.0f;

    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    VulkanCarrierSession carrier;
    assert(carrier.Configure(config, performance));

    const auto ready = carrier.Resolve(
        Packet(config.generation, 1));
    assert(ready);
    assert(ready.acquire.frame.api == GraphicsApi::Vulkan);
    assert(ready.session.disposition == NrSessionDisposition::Ready);
    assert(ready.session.configurationGeneration ==
           config.generation);
    assert(ready.session.decision.pipeline.api ==
           GraphicsApi::Vulkan);
    assert(carrier.State().resolvedFrames == 1);

    const auto work = carrier.BeginWork(ready, 11);
    assert(work);
    assert(carrier.CanExecuteWork(*work));
    assert(carrier.ClaimExecuteWork(*work));
    assert(!carrier.ClaimExecuteWork(*work));
    assert(carrier.SubmitWork(*work));

    auto invalidAcquire = Packet(config.generation, 2);
    invalidAcquire.acquire.color.image.usage = VulkanUsageStorage;
    const auto rejectedAcquire = carrier.Resolve(invalidAcquire);
    assert(!rejectedAcquire);
    assert(rejectedAcquire.acquire.failure ==
           VulkanAcquireFailure::InvalidUsage);
    assert(carrier.State().resolvedFrames == 1);

    const auto stale = carrier.Resolve(
        Packet(config.generation - 1, 3));
    assert(!stale);
    assert(!stale.acquire.attempted);
    assert(stale.session.disposition ==
           NrSessionDisposition::StaleConfiguration);

    auto wrongApi = Packet(config.generation, 4);
    wrongApi.game.api = GraphicsApi::D3D12;
    const auto mismatched = carrier.Resolve(wrongApi);
    assert(!mismatched);
    assert(!mismatched.acquire.attempted);
    assert(mismatched.session.disposition ==
           NrSessionDisposition::InvalidFrame);

    RuntimeConfig disabled = config;
    ++disabled.generation;
    disabled.enabled = false;
    assert(carrier.Configure(disabled, performance));
    const auto passThrough = carrier.Resolve(
        Packet(disabled.generation, 5));
    assert(!passThrough);
    assert(!passThrough.acquire.attempted);
    assert(passThrough.session.disposition ==
           NrSessionDisposition::Disabled);

    carrier.Reset();
    const auto afterReset = carrier.Resolve(
        Packet(config.generation, 6));
    assert(!afterReset);
    assert(!afterReset.acquire.attempted);
    assert(afterReset.session.disposition ==
           NrSessionDisposition::NotConfigured);
    return 0;
}
