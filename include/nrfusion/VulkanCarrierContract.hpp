#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cstdint>
#include <limits>

namespace nrfusion {

enum class VulkanHandleOwnership : std::uint8_t {
    Invalid,
    Borrowed,
    OwnedDuplicate
};

enum class VulkanImageLayoutIntent : std::uint8_t {
    Undefined,
    General,
    ShaderRead,
    TransferSource,
    TransferDestination
};

enum class VulkanQueueOwnership : std::uint8_t {
    Invalid,
    External,
    Local,
    Concurrent
};

enum VulkanImageUsageBits : std::uint32_t {
    VulkanUsageSampled = 1u << 0,
    VulkanUsageStorage = 1u << 1,
    VulkanUsageTransferSource = 1u << 2,
    VulkanUsageTransferDestination = 1u << 3
};

struct VulkanExternalMemoryContract {
    std::uintptr_t handle = 0;
    ResourceFormat format = ResourceFormat::Unknown;
    Resolution resolution{};
    std::uint32_t usage = 0;
    std::uint64_t allocationSize = 0;
    std::uint32_t memoryTypeBits = 0;
    std::uint32_t memoryTypeIndex = std::numeric_limits<std::uint32_t>::max();
    VulkanHandleOwnership handleOwnership = VulkanHandleOwnership::Invalid;
    VulkanImageLayoutIntent initialLayout = VulkanImageLayoutIntent::Undefined;
    VulkanImageLayoutIntent executionLayout = VulkanImageLayoutIntent::General;
    VulkanQueueOwnership queueOwnership = VulkanQueueOwnership::Invalid;
    std::uint32_t executionQueueFamily =
        std::numeric_limits<std::uint32_t>::max();
};

enum class VulkanTimelineDirection : std::uint8_t {
    Wait,
    Signal
};

struct VulkanTimelineContract {
    std::uintptr_t handle = 0;
    std::uint64_t value = 0;
    VulkanTimelineDirection direction = VulkanTimelineDirection::Wait;
    VulkanHandleOwnership handleOwnership = VulkanHandleOwnership::Invalid;
    bool timeline = false;
};

struct VulkanAcquireContract {
    FrameId frameId = 0;
    std::uint64_t configurationGeneration = 0;
    VulkanExternalMemoryContract color{};
    VulkanExternalMemoryContract output{};
    VulkanTimelineContract producer{};
    VulkanTimelineContract consumer{};
};

enum class VulkanContractFailure : std::uint8_t {
    None,
    InvalidIdentity,
    MissingHandle,
    InvalidExtent,
    UnknownFormat,
    MissingUsage,
    InvalidAllocation,
    InvalidMemoryType,
    InvalidHandleOwnership,
    InvalidLayout,
    InvalidQueueOwnership,
    MissingTimelineSemaphore,
    NonTimelineSemaphore,
    InvalidTimelineValue,
    InvalidTimelineDirection
};

struct VulkanContractValidation {
    VulkanContractFailure failure = VulkanContractFailure::None;
    constexpr explicit operator bool() const noexcept {
        return failure == VulkanContractFailure::None;
    }
};

VulkanContractValidation ValidateVulkanExternalMemory(
    const VulkanExternalMemoryContract& memory,
    std::uint32_t requiredUsageAny) noexcept;

VulkanContractValidation ValidateVulkanTimeline(
    const VulkanTimelineContract& timeline,
    VulkanTimelineDirection expectedDirection) noexcept;

VulkanContractValidation ValidateVulkanAcquireContract(
    const VulkanAcquireContract& contract) noexcept;

} // namespace nrfusion
