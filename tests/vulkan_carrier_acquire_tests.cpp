#include "nrfusion/VulkanCarrierAcquire.hpp"

#include <cassert>
#include <limits>

using namespace nrfusion;

namespace {

VulkanNativeImageFacts Image(
    std::uint64_t id,
    std::uint32_t usage,
    VulkanImageLayoutIntent layout =
        VulkanImageLayoutIntent::General) {
    VulkanNativeImageFacts image{};
    image.opaqueId = id;
    image.resolution = {1920, 1080};
    image.format = ResourceFormat::Rgba16Float;
    image.usage = usage;
    image.layout = layout;
    image.queueFamilyIndex = 3;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.sampleCount = 1;
    image.image2D = true;
    return image;
}

VulkanNativeAcquireInput BaseInput() {
    VulkanNativeAcquireInput input{};
    input.identity.frameId = 41;
    input.identity.hostFrameToken = 77;
    input.identity.viewId = 5;
    input.identity.configurationGeneration = 9;
    input.color.image = Image(
        0x1000,
        VulkanUsageSampled | VulkanUsageTransferSource);
    input.color.provenance = ResourceProvenance::GameNative;
    input.color.reliability = ResourceReliability::Reliable;
    input.output = Image(
        0x2000,
        VulkanUsageStorage | VulkanUsageTransferDestination);
    input.hdr = true;
    return input;
}

WorkTicket Ticket() {
    WorkTicket ticket{};
    ticket.id = 12;
    ticket.session = 3;
    ticket.sourceFrame = 41;
    ticket.viewKey = 5;
    ticket.configurationGeneration = 9;
    ticket.workingScale = 1.0f;
    return ticket;
}

} // namespace

int main() {
    const auto base = BaseInput();
    const auto acquired = AcquireVulkanFrame(base);
    assert(acquired);
    assert(acquired.frame.api == GraphicsApi::Vulkan);
    assert(acquired.frame.ReadyForCore());
    assert(acquired.outputOpaqueId == 0x2000);
    assert(acquired.queueFamilyIndex == 3);
    assert(acquired.colorLayout == VulkanImageLayoutIntent::General);

    const auto work = BuildVulkanCarrierWork(acquired.frame, Ticket());
    assert(work);
    assert(work->ticket.id == 12);
    assert(work->color.opaqueId == 0x1000);
    assert(work->workingScale == 1.0f);

    auto invalidIdentity = base;
    invalidIdentity.identity.configurationGeneration = 0;
    assert(AcquireVulkanFrame(invalidIdentity).failure ==
           VulkanAcquireFailure::InvalidIdentity);

    auto noColor = base;
    noColor.color = {};
    assert(AcquireVulkanFrame(noColor).failure ==
           VulkanAcquireFailure::MissingColor);

    auto noOutput = base;
    noOutput.output = {};
    assert(AcquireVulkanFrame(noOutput).failure ==
           VulkanAcquireFailure::MissingOutput);

    auto badUsage = base;
    badUsage.color.image.usage = VulkanUsageStorage;
    assert(AcquireVulkanFrame(badUsage).failure ==
           VulkanAcquireFailure::InvalidUsage);

    auto undefinedLayout = base;
    undefinedLayout.color.image.layout =
        VulkanImageLayoutIntent::Undefined;
    assert(AcquireVulkanFrame(undefinedLayout).failure ==
           VulkanAcquireFailure::InvalidLayout);

    auto noQueue = base;
    noQueue.output.queueFamilyIndex =
        std::numeric_limits<std::uint32_t>::max();
    assert(AcquireVulkanFrame(noQueue).failure ==
           VulkanAcquireFailure::InvalidQueueFamily);

    auto queueMismatch = base;
    queueMismatch.output.queueFamilyIndex = 4;
    assert(AcquireVulkanFrame(queueMismatch).failure ==
           VulkanAcquireFailure::InvalidQueueFamily);

    auto noEvidence = base;
    noEvidence.color.provenance = ResourceProvenance::Unknown;
    assert(AcquireVulkanFrame(noEvidence).failure ==
           VulkanAcquireFailure::InvalidEvidence);

    auto wrongSize = base;
    wrongSize.output.resolution = {1280, 720};
    assert(AcquireVulkanFrame(wrongSize).failure ==
           VulkanAcquireFailure::ColorResolutionMismatch);

    auto staleTicket = Ticket();
    staleTicket.sourceFrame = 40;
    assert(!BuildVulkanCarrierWork(acquired.frame, staleTicket));

    auto staleGeneration = Ticket();
    staleGeneration.configurationGeneration = 8;
    assert(!BuildVulkanCarrierWork(
        acquired.frame, staleGeneration));

    auto badScale = Ticket();
    badScale.workingScale = 0.0f;
    assert(!BuildVulkanCarrierWork(acquired.frame, badScale));

    return 0;
}
