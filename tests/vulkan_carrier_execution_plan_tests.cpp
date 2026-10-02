#include "nrfusion/VulkanCarrierExecutionPlan.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

VulkanNativeImageFacts Image(
    std::uint64_t id, std::uint32_t usage,
    VulkanImageLayoutIntent layout = VulkanImageLayoutIntent::General) {
    VulkanNativeImageFacts image{};
    image.opaqueId = id;
    image.resolution = {1920, 1080};
    image.format = ResourceFormat::Rgba16Float;
    image.usage = usage;
    image.layout = layout;
    image.queueFamilyIndex = 2;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.sampleCount = 1;
    image.image2D = true;
    return image;
}

VulkanCarrierFramePacket Packet(
    std::uint64_t generation, FrameId frameId) {
    VulkanCarrierFramePacket packet{};
    packet.game.api = GraphicsApi::Vulkan;
    packet.acquire.identity.frameId = frameId;
    packet.acquire.identity.viewId = 11;
    packet.acquire.identity.configurationGeneration = generation;
    packet.acquire.resourceGeneration = frameId;
    packet.acquire.color.image = Image(
        0x1000 + frameId,
        VulkanUsageSampled | VulkanUsageTransferSource);
    packet.acquire.color.provenance = ResourceProvenance::GameNative;
    packet.acquire.color.reliability = ResourceReliability::Reliable;
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

VulkanCarrierExecutionResources Resources(
    const VulkanCarrierFrameResult& frame) {
    VulkanCarrierExecutionResources resources{};
    resources.color.image = frame.acquire.colorFacts;
    resources.color.provenance = frame.acquire.frame.color.provenance;
    resources.color.reliability = frame.acquire.frame.color.reliability;
    resources.output = frame.acquire.outputFacts;
    resources.colorOwnership = VulkanQueueOwnership::Local;
    resources.outputOwnership = VulkanQueueOwnership::Local;
    resources.recreationGeneration = frame.acquire.resourceGeneration;
    resources.compose = VulkanCarrierComposeIntent::CopyOrBlit;
    return resources;
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
    const auto frame = carrier.Resolve(Packet(config.generation, 1));
    assert(frame);
    const auto work = carrier.BeginWork(frame, 11);
    assert(work);

    auto resources = Resources(frame);
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, resources).failure ==
           VulkanCarrierExecutionFailure::WorkNotClaimed);

    auto stale = *work;
    stale.ticket.sourceFrame = 2;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, stale, resources).failure ==
           VulkanCarrierExecutionFailure::WorkFrameMismatch);

    auto oldGeneration = *work;
    --oldGeneration.ticket.configurationGeneration;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, oldGeneration, resources).failure ==
           VulkanCarrierExecutionFailure::WorkGenerationMismatch);

    assert(carrier.ClaimExecuteWork(*work));

    const auto valid = BuildVulkanCarrierExecutionPlan(
        carrier, frame, *work, resources);
    assert(valid);
    assert(valid.plan.ticket == work->ticket);
    assert(valid.plan.submissionEpoch == work->submissionEpoch);
    assert(valid.plan.queueFamilyIndex == frame.acquire.queueFamilyIndex);
    assert(valid.plan.recreationGeneration ==
           frame.acquire.resourceGeneration);
    assert(valid.plan.compose == VulkanCarrierComposeIntent::CopyOrBlit);

    auto badQueue = resources;
    ++badQueue.output.queueFamilyIndex;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, badQueue).failure ==
           VulkanCarrierExecutionFailure::InvalidQueueFamily);

    auto badLayout = resources;
    badLayout.color.image.layout = VulkanImageLayoutIntent::Undefined;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, badLayout).failure ==
           VulkanCarrierExecutionFailure::InvalidLayout);

    auto badUsage = resources;
    badUsage.output.usage = VulkanUsageStorage;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, badUsage).failure ==
           VulkanCarrierExecutionFailure::InvalidUsage);

    auto badDimensions = resources;
    badDimensions.output.resolution = {1280, 720};
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, badDimensions).failure ==
           VulkanCarrierExecutionFailure::InvalidDimensions);

    auto staleResources = resources;
    ++staleResources.recreationGeneration;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, staleResources).failure ==
           VulkanCarrierExecutionFailure::RecreationGenerationMismatch);

    auto wrongResource = resources;
    ++wrongResource.output.opaqueId;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, wrongResource).failure ==
           VulkanCarrierExecutionFailure::ResourceIdentityMismatch);

    auto badProvenance = resources;
    badProvenance.color.provenance = ResourceProvenance::DlssContract;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, badProvenance).failure ==
           VulkanCarrierExecutionFailure::InvalidProvenance);

    auto noGeneration = resources;
    noGeneration.recreationGeneration = 0;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, noGeneration).failure ==
           VulkanCarrierExecutionFailure::InvalidRecreationGeneration);

    auto external = resources;
    external.externalInterop = true;
    external.colorOwnership = VulkanQueueOwnership::External;
    external.outputOwnership = VulkanQueueOwnership::External;
    external.producer = {
        0x3000, 4, VulkanTimelineDirection::Wait,
        VulkanHandleOwnership::Borrowed, true};
    external.consumer = {
        0x4000, 5, VulkanTimelineDirection::Signal,
        VulkanHandleOwnership::Borrowed, true};
    const auto externalPlan = BuildVulkanCarrierExecutionPlan(
        carrier, frame, *work, external);
    assert(externalPlan);
    assert(externalPlan.plan.externalInterop);

    auto missingProducer = external;
    missingProducer.producer.handle = 0;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, missingProducer).failure ==
           VulkanCarrierExecutionFailure::InvalidTimeline);

    auto wrongSignal = external;
    wrongSignal.consumer.direction = VulkanTimelineDirection::Wait;
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, wrongSignal).failure ==
           VulkanCarrierExecutionFailure::InvalidTimeline);

    assert(carrier.ConsumeClaimedExecution(*work));
    assert(!carrier.ConsumeClaimedExecution(*work));
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *work, resources).failure ==
           VulkanCarrierExecutionFailure::WorkNotClaimed);
    assert(carrier.SubmitWork(*work));

    const auto abandoned = carrier.BeginWork(frame, 11);
    assert(abandoned);
    assert(carrier.ClaimExecuteWork(*abandoned));
    assert(carrier.AbandonWork(*abandoned));
    assert(!carrier.ConsumeClaimedExecution(*abandoned));
    assert(BuildVulkanCarrierExecutionPlan(
               carrier, frame, *abandoned, resources).failure ==
           VulkanCarrierExecutionFailure::WorkNotClaimed);
    return 0;
}
