#include "nrfusion/VulkanCarrierExecutor.hpp"

#include <cassert>
#include <cstdint>

using namespace nrfusion;

namespace {

std::uint32_t barrierCount = 0;
std::uint32_t copyCount = 0;
VkImageLayout copiedSourceLayout = VK_IMAGE_LAYOUT_UNDEFINED;
VkImageLayout copiedDestinationLayout = VK_IMAGE_LAYOUT_UNDEFINED;

VKAPI_ATTR void VKAPI_CALL FakeBarrier(
    VkCommandBuffer,
    VkPipelineStageFlags,
    VkPipelineStageFlags,
    VkDependencyFlags,
    std::uint32_t,
    const VkMemoryBarrier*,
    std::uint32_t,
    const VkBufferMemoryBarrier*,
    std::uint32_t imageBarrierCount,
    const VkImageMemoryBarrier*) {
    barrierCount += imageBarrierCount;
}

VKAPI_ATTR void VKAPI_CALL FakeCopy(
    VkCommandBuffer,
    VkImage,
    VkImageLayout sourceLayout,
    VkImage,
    VkImageLayout destinationLayout,
    std::uint32_t,
    const VkImageCopy*) {
    ++copyCount;
    copiedSourceLayout = sourceLayout;
    copiedDestinationLayout = destinationLayout;
}

VulkanNativeImageFacts Image(
    std::uint64_t id, std::uint32_t usage) {
    VulkanNativeImageFacts image{};
    image.opaqueId = id;
    image.resolution = {64, 64};
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

VulkanCarrierFramePacket Packet(std::uint64_t generation) {
    VulkanCarrierFramePacket packet{};
    packet.game.api = GraphicsApi::Vulkan;
    packet.acquire.identity.frameId = 1;
    packet.acquire.identity.viewId = 11;
    packet.acquire.identity.configurationGeneration = generation;
    packet.acquire.resourceGeneration = 1;
    packet.acquire.color.image = Image(
        0x1001, VulkanUsageSampled | VulkanUsageTransferSource);
    packet.acquire.color.provenance = ResourceProvenance::GameNative;
    packet.acquire.color.reliability = ResourceReliability::Reliable;
    packet.acquire.output = Image(
        0x2001, VulkanUsageStorage | VulkanUsageTransferDestination);
    packet.capabilities.syntheticVulkan = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    packet.telemetry.frameGpuMs = 8.0;
    packet.telemetry.sourceFps = 60.0;
    packet.telemetry.processedFps = 60.0;
    return packet;
}

VulkanCarrierNativeResources Resources(
    const VulkanCarrierFrameResult& frame) {
    VulkanCarrierNativeResources resources{};
    resources.facts.color.image = frame.acquire.colorFacts;
    resources.facts.color.provenance =
        frame.acquire.frame.color.provenance;
    resources.facts.color.reliability =
        frame.acquire.frame.color.reliability;
    resources.facts.output = frame.acquire.outputFacts;
    resources.facts.colorOwnership = VulkanQueueOwnership::Local;
    resources.facts.outputOwnership = VulkanQueueOwnership::Local;
    resources.facts.recreationGeneration =
        frame.acquire.resourceGeneration;
    resources.facts.compose = VulkanCarrierComposeIntent::CopyOrBlit;
    resources.color = reinterpret_cast<VkImage>(
        static_cast<std::uintptr_t>(0x1001));
    resources.output = reinterpret_cast<VkImage>(
        static_cast<std::uintptr_t>(0x2001));
    return resources;
}

VulkanCarrierExecutionContext Context() {
    VulkanCarrierExecutionContext context{};
    context.native.instance = reinterpret_cast<VkInstance>(1);
    context.native.physicalDevice = reinterpret_cast<VkPhysicalDevice>(2);
    context.native.device = reinterpret_cast<VkDevice>(3);
    context.native.queue = reinterpret_cast<VkQueue>(4);
    context.native.queueFamilyIndex = 2;
    context.commands.pipelineBarrier = FakeBarrier;
    context.commands.copyImage = FakeCopy;
    return context;
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
    const auto frame = carrier.Resolve(Packet(config.generation));
    assert(frame);

    const auto invalidWork = carrier.BeginWork(frame, 11);
    assert(invalidWork);
    assert(carrier.ClaimExecuteWork(*invalidWork));
    auto resources = Resources(frame);
    auto context = Context();

    VulkanCarrierExecutor executor;
    const auto invalidCommand = executor.Execute(
        VK_NULL_HANDLE, carrier, context,
        frame, *invalidWork, resources);
    assert(invalidCommand.failure ==
           VulkanCarrierExecuteFailure::InvalidCommandBuffer);
    assert(!invalidCommand.claimConsumed);
    assert(carrier.HasClaimedExecution(*invalidWork));
    assert(carrier.AbandonWork(*invalidWork));

    const auto work = carrier.BeginWork(frame, 11);
    assert(work);
    assert(carrier.ClaimExecuteWork(*work));

    barrierCount = 0;
    copyCount = 0;
    const auto recorded = executor.Execute(
        reinterpret_cast<VkCommandBuffer>(5),
        carrier, context, frame, *work, resources);
    assert(recorded);
    assert(recorded.claimConsumed);
    assert(recorded.plan.ticket == work->ticket);
    assert(barrierCount == 4);
    assert(copyCount == 1);
    assert(copiedSourceLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    assert(copiedDestinationLayout ==
           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    assert(!carrier.HasClaimedExecution(*work));

    const auto duplicate = executor.Execute(
        reinterpret_cast<VkCommandBuffer>(5),
        carrier, context, frame, *work, resources);
    assert(duplicate.failure == VulkanCarrierExecuteFailure::Planning);
    assert(duplicate.planningFailure ==
           VulkanCarrierExecutionFailure::WorkNotClaimed);
    assert(carrier.SubmitWork(*work));

    const auto externalWork = carrier.BeginWork(frame, 11);
    assert(externalWork);
    assert(carrier.ClaimExecuteWork(*externalWork));
    auto external = resources;
    external.facts.externalInterop = true;
    external.facts.colorOwnership = VulkanQueueOwnership::External;
    external.facts.outputOwnership = VulkanQueueOwnership::External;
    external.facts.producer = {
        0x3000, 8, VulkanTimelineDirection::Wait,
        VulkanHandleOwnership::Borrowed, true};
    external.facts.consumer = {
        0x4000, 9, VulkanTimelineDirection::Signal,
        VulkanHandleOwnership::Borrowed, true};

    barrierCount = 0;
    copyCount = 0;
    const auto externalRecorded = executor.Execute(
        reinterpret_cast<VkCommandBuffer>(6),
        carrier, context, frame, *externalWork, external);
    assert(externalRecorded);
    assert(externalRecorded.plan.externalInterop);
    assert(externalRecorded.plan.producer.value == 8);
    assert(externalRecorded.plan.consumer.value == 9);
    assert(barrierCount == 4);
    assert(copyCount == 1);
    assert(carrier.AbandonWork(*externalWork));
    return 0;
}
