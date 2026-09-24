#include "VulkanNativeHarness.hpp"

#include "nrfusion/VulkanCarrierExecutor.hpp"

#include <windows.h>

#include <cassert>
#include <cstdint>

using namespace nrfusion;
using namespace nrfusion::test;

namespace {

constexpr int kSkip = 77;
constexpr std::uint32_t kCycles = 32;

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_VULKAN_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

std::uint64_t OpaqueId(VkImage image) noexcept {
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(image));
}

VulkanNativeImageFacts ImageFacts(
    VkImage image, std::uint32_t usage,
    std::uint32_t queueFamily) {
    VulkanNativeImageFacts facts{};
    facts.opaqueId = OpaqueId(image);
    facts.resolution = {64, 64};
    facts.format = ResourceFormat::Rgba16Float;
    facts.usage = usage;
    facts.layout = VulkanImageLayoutIntent::General;
    facts.queueFamilyIndex = queueFamily;
    facts.mipLevels = 1;
    facts.arrayLayers = 1;
    facts.sampleCount = 1;
    facts.image2D = true;
    return facts;
}

VulkanCarrierFramePacket Packet(
    std::uint64_t generation,
    FrameId frameId,
    VkImage color,
    VkImage output,
    std::uint32_t queueFamily) {
    VulkanCarrierFramePacket packet{};
    packet.game.api = GraphicsApi::Vulkan;
    packet.acquire.identity.frameId = frameId;
    packet.acquire.identity.viewId = 1;
    packet.acquire.identity.configurationGeneration = generation;
    packet.acquire.color.image = ImageFacts(
        color, VulkanUsageTransferSource, queueFamily);
    packet.acquire.color.provenance = ResourceProvenance::GameNative;
    packet.acquire.color.reliability = ResourceReliability::Reliable;
    packet.acquire.output = ImageFacts(
        output, VulkanUsageTransferDestination, queueFamily);
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
    const VulkanCarrierFrameResult& frame,
    VkImage color,
    VkImage output,
    std::uint64_t generation) {
    VulkanCarrierNativeResources resources{};
    resources.facts.color.image = frame.acquire.colorFacts;
    resources.facts.color.provenance =
        frame.acquire.frame.color.provenance;
    resources.facts.color.reliability =
        frame.acquire.frame.color.reliability;
    resources.facts.output = frame.acquire.outputFacts;
    resources.facts.colorOwnership = VulkanQueueOwnership::Local;
    resources.facts.outputOwnership = VulkanQueueOwnership::Local;
    resources.facts.recreationGeneration = generation;
    resources.facts.compose = VulkanCarrierComposeIntent::CopyOrBlit;
    resources.color = color;
    resources.output = output;
    return resources;
}

bool InitializeLayouts(
    VulkanNativeHarness& harness,
    const VulkanCommandDispatch& dispatch,
    VkImage color,
    VkImage output) {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (!harness.CreatePrimaryCommandBuffer(pool, command) ||
        !harness.Begin(command)) {
        harness.DestroyCommandPool(pool);
        return false;
    }
    const bool recorded =
        RecordVulkanImageTransition(
            dispatch, command, color,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL) &&
        RecordVulkanImageTransition(
            dispatch, command, output,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const bool submitted =
        recorded && harness.EndSubmitAndWait(command);
    harness.DestroyCommandPool(pool);
    return submitted;
}

bool ExecuteCycle(
    VulkanNativeHarness& harness,
    VulkanCarrierSession& carrier,
    const VulkanCarrierExecutionContext& context,
    std::uint64_t configGeneration,
    std::uint64_t cycle) {
    VkImage color = VK_NULL_HANDLE;
    VkImage output = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkDeviceMemory outputMemory = VK_NULL_HANDLE;
    if (!harness.CreateBoundImage(color, colorMemory) ||
        !harness.CreateBoundImage(output, outputMemory)) {
        harness.DestroyBoundImage(output, outputMemory);
        harness.DestroyBoundImage(color, colorMemory);
        return false;
    }

    bool ok = InitializeLayouts(
        harness, context.commands, color, output);
    const auto frame = carrier.Resolve(Packet(
        configGeneration, cycle, color, output,
        context.native.queueFamilyIndex));
    const auto work = ok ? carrier.BeginWork(frame, 1) : std::nullopt;
    ok = ok && frame && work && carrier.ClaimExecuteWork(*work);

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (ok) {
        ok = harness.CreatePrimaryCommandBuffer(pool, command) &&
             harness.Begin(command);
    }
    if (ok) {
        VulkanCarrierExecutor executor;
        const auto resources = Resources(frame, color, output, cycle);
        const auto recorded = executor.Execute(
            command, carrier, context, frame, *work, resources);
        ok = recorded &&
             harness.EndSubmitAndWait(command) &&
             carrier.SubmitWork(*work) &&
             carrier.AbandonWork(*work);
    } else if (work) {
        carrier.AbandonWork(*work);
    }

    harness.DestroyCommandPool(pool);
    harness.DestroyBoundImage(output, outputMemory);
    harness.DestroyBoundImage(color, colorMemory);
    return ok;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    VulkanNativeHarness harness;
    if (!harness.Open(required)) return required ? 1 : kSkip;

    const VulkanContextContract contract = harness.ContextContract();
    const VulkanNativeContext native = MakeVulkanNativeContext(contract);
    const VulkanCommandDispatch commands = harness.CommandDispatch();
    assert(native.Valid());
    assert(commands.Valid());

    RuntimeConfig config{};
    config.generation = 7;
    config.enabled = true;
    config.targetFps = 60.0f;
    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    VulkanCarrierSession carrier;
    assert(carrier.Configure(config, performance));

    VulkanCarrierExecutionContext context{};
    context.native = native;
    context.commands = commands;

    for (std::uint64_t cycle = 1; cycle <= kCycles; ++cycle)
        assert(ExecuteCycle(
            harness, carrier, context, config.generation, cycle));

    harness.Close();
    return 0;
}
