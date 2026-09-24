#include "nrfusion/VulkanCarrierContract.hpp"

namespace nrfusion {
namespace {

bool ValidHandleOwnership(VulkanHandleOwnership ownership) noexcept {
    return ownership == VulkanHandleOwnership::Borrowed ||
           ownership == VulkanHandleOwnership::OwnedDuplicate;
}

VulkanContractValidation Failure(VulkanContractFailure failure) noexcept {
    return {failure};
}

} // namespace

VulkanContractValidation ValidateVulkanExternalMemory(
    const VulkanExternalMemoryContract& memory,
    std::uint32_t requiredUsageAny) noexcept {
    if (memory.handle == 0) return Failure(VulkanContractFailure::MissingHandle);
    if (memory.resolution.width == 0 || memory.resolution.height == 0)
        return Failure(VulkanContractFailure::InvalidExtent);
    if (memory.format == ResourceFormat::Unknown)
        return Failure(VulkanContractFailure::UnknownFormat);
    if (memory.usage == 0 ||
        (requiredUsageAny != 0 && (memory.usage & requiredUsageAny) == 0))
        return Failure(VulkanContractFailure::MissingUsage);
    if (memory.allocationSize == 0)
        return Failure(VulkanContractFailure::InvalidAllocation);
    if (memory.memoryTypeBits == 0 ||
        memory.memoryTypeIndex >= 32 ||
        (memory.memoryTypeBits & (1u << memory.memoryTypeIndex)) == 0)
        return Failure(VulkanContractFailure::InvalidMemoryType);
    if (!ValidHandleOwnership(memory.handleOwnership))
        return Failure(VulkanContractFailure::InvalidHandleOwnership);
    if (memory.initialLayout == VulkanImageLayoutIntent::Undefined &&
        memory.queueOwnership != VulkanQueueOwnership::External)
        return Failure(VulkanContractFailure::InvalidLayout);
    if (memory.queueOwnership == VulkanQueueOwnership::Invalid)
        return Failure(VulkanContractFailure::InvalidQueueOwnership);
    if (memory.queueOwnership == VulkanQueueOwnership::Local &&
        memory.executionQueueFamily == std::numeric_limits<std::uint32_t>::max())
        return Failure(VulkanContractFailure::InvalidQueueOwnership);
    return {};
}

VulkanContractValidation ValidateVulkanTimeline(
    const VulkanTimelineContract& timeline,
    VulkanTimelineDirection expectedDirection) noexcept {
    if (timeline.handle == 0)
        return Failure(VulkanContractFailure::MissingTimelineSemaphore);
    if (!timeline.timeline)
        return Failure(VulkanContractFailure::NonTimelineSemaphore);
    if (timeline.value == 0)
        return Failure(VulkanContractFailure::InvalidTimelineValue);
    if (timeline.direction != expectedDirection)
        return Failure(VulkanContractFailure::InvalidTimelineDirection);
    if (!ValidHandleOwnership(timeline.handleOwnership))
        return Failure(VulkanContractFailure::InvalidHandleOwnership);
    return {};
}

VulkanContractValidation ValidateVulkanAcquireContract(
    const VulkanAcquireContract& contract) noexcept {
    if (contract.frameId == 0 || contract.configurationGeneration == 0)
        return Failure(VulkanContractFailure::InvalidIdentity);

    auto validation = ValidateVulkanExternalMemory(
        contract.color, VulkanUsageSampled | VulkanUsageTransferSource);
    if (!validation) return validation;

    validation = ValidateVulkanExternalMemory(
        contract.output, VulkanUsageStorage | VulkanUsageTransferDestination);
    if (!validation) return validation;

    validation = ValidateVulkanTimeline(
        contract.producer, VulkanTimelineDirection::Wait);
    if (!validation) return validation;

    return ValidateVulkanTimeline(
        contract.consumer, VulkanTimelineDirection::Signal);
}

} // namespace nrfusion
