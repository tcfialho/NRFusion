#include "nrfusion/VulkanCarrierContract.hpp"

#include <cassert>
#include <type_traits>

using namespace nrfusion;

namespace {

VulkanExternalMemoryContract Memory(
    std::uintptr_t handle, std::uint32_t usage) {
    VulkanExternalMemoryContract memory{};
    memory.handle = handle;
    memory.format = ResourceFormat::Rgba16Float;
    memory.resolution = {1920, 1080};
    memory.usage = usage;
    memory.allocationSize = 1920ull * 1080ull * 8ull;
    memory.memoryTypeBits = 0b1010u;
    memory.memoryTypeIndex = 1;
    memory.handleOwnership = VulkanHandleOwnership::OwnedDuplicate;
    memory.initialLayout = VulkanImageLayoutIntent::General;
    memory.executionLayout = VulkanImageLayoutIntent::General;
    memory.queueOwnership = VulkanQueueOwnership::Local;
    memory.executionQueueFamily = 2;
    return memory;
}

VulkanTimelineContract Timeline(
    std::uintptr_t handle, std::uint64_t value,
    VulkanTimelineDirection direction) {
    VulkanTimelineContract timeline{};
    timeline.handle = handle;
    timeline.value = value;
    timeline.direction = direction;
    timeline.handleOwnership = VulkanHandleOwnership::OwnedDuplicate;
    timeline.timeline = true;
    return timeline;
}

VulkanAcquireContract BaseContract() {
    VulkanAcquireContract contract{};
    contract.frameId = 42;
    contract.configurationGeneration = 7;
    contract.color = Memory(
        0x1000, VulkanUsageSampled | VulkanUsageTransferSource);
    contract.output = Memory(
        0x2000, VulkanUsageStorage | VulkanUsageTransferDestination);
    contract.producer = Timeline(0x3000, 11, VulkanTimelineDirection::Wait);
    contract.consumer = Timeline(0x4000, 12, VulkanTimelineDirection::Signal);
    return contract;
}

} // namespace

int main() {
    static_assert(std::is_trivially_copyable_v<VulkanExternalMemoryContract>);
    static_assert(std::is_trivially_copyable_v<VulkanTimelineContract>);
    static_assert(std::is_trivially_copyable_v<VulkanAcquireContract>);

    VulkanContextContract context{};
    context.instance = 0x10;
    context.physicalDevice = 0x20;
    context.device = 0x30;
    context.queue = 0x40;
    context.queueFamilyIndex = 3;
    assert(ValidateVulkanContextContract(context));
    auto missingPhysicalDevice = context;
    missingPhysicalDevice.physicalDevice = 0;
    assert(ValidateVulkanContextContract(missingPhysicalDevice).failure ==
           VulkanContractFailure::InvalidNativeContext);

    const auto selected = SelectVulkanMemoryTypeIndex(0b10110u, 0b01100u);
    assert(selected && *selected == 2u);
    assert(!SelectVulkanMemoryTypeIndex(0b00010u, 0b00100u));

    const auto base = BaseContract();
    assert(ValidateVulkanAcquireContract(base));

    auto missingHandle = base;
    missingHandle.color.handle = 0;
    assert(ValidateVulkanAcquireContract(missingHandle).failure ==
           VulkanContractFailure::MissingHandle);

    auto badExtent = base;
    badExtent.color.resolution.width = 0;
    assert(ValidateVulkanAcquireContract(badExtent).failure ==
           VulkanContractFailure::InvalidExtent);

    auto unknownFormat = base;
    unknownFormat.color.format = ResourceFormat::Unknown;
    assert(ValidateVulkanAcquireContract(unknownFormat).failure ==
           VulkanContractFailure::UnknownFormat);

    auto missingUsage = base;
    missingUsage.color.usage = VulkanUsageStorage;
    assert(ValidateVulkanAcquireContract(missingUsage).failure ==
           VulkanContractFailure::MissingUsage);

    auto badMemoryType = base;
    badMemoryType.color.memoryTypeIndex = 2;
    assert(ValidateVulkanAcquireContract(badMemoryType).failure ==
           VulkanContractFailure::InvalidMemoryType);

    auto noOwnership = base;
    noOwnership.color.handleOwnership = VulkanHandleOwnership::Invalid;
    assert(ValidateVulkanAcquireContract(noOwnership).failure ==
           VulkanContractFailure::InvalidHandleOwnership);

    auto externalUndefined = base;
    externalUndefined.color.initialLayout = VulkanImageLayoutIntent::Undefined;
    externalUndefined.color.queueOwnership = VulkanQueueOwnership::External;
    assert(ValidateVulkanAcquireContract(externalUndefined));

    auto localUndefined = externalUndefined;
    localUndefined.color.queueOwnership = VulkanQueueOwnership::Local;
    assert(ValidateVulkanAcquireContract(localUndefined).failure ==
           VulkanContractFailure::InvalidLayout);

    auto noQueue = base;
    noQueue.color.queueOwnership = VulkanQueueOwnership::Invalid;
    assert(ValidateVulkanAcquireContract(noQueue).failure ==
           VulkanContractFailure::InvalidQueueOwnership);

    auto noQueueFamily = base;
    noQueueFamily.color.executionQueueFamily =
        std::numeric_limits<std::uint32_t>::max();
    assert(ValidateVulkanAcquireContract(noQueueFamily).failure ==
           VulkanContractFailure::InvalidQueueOwnership);

    auto binarySync = base;
    binarySync.producer.timeline = false;
    assert(ValidateVulkanAcquireContract(binarySync).failure ==
           VulkanContractFailure::NonTimelineSemaphore);

    auto zeroValue = base;
    zeroValue.consumer.value = 0;
    assert(ValidateVulkanAcquireContract(zeroValue).failure ==
           VulkanContractFailure::InvalidTimelineValue);

    auto wrongDirection = base;
    wrongDirection.consumer.direction = VulkanTimelineDirection::Wait;
    assert(ValidateVulkanAcquireContract(wrongDirection).failure ==
           VulkanContractFailure::InvalidTimelineDirection);

    auto badIdentity = base;
    badIdentity.configurationGeneration = 0;
    assert(ValidateVulkanAcquireContract(badIdentity).failure ==
           VulkanContractFailure::InvalidIdentity);

    return 0;
}
