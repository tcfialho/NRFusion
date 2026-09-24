#pragma once

#include "nrfusion/VulkanCarrierSession.hpp"

#include <cstdint>
#include <limits>

namespace nrfusion {

enum class VulkanCarrierComposeIntent : std::uint8_t {
    Invalid,
    CopyOrBlit
};

struct VulkanCarrierExecutionResources {
    VulkanNativeResourceCapture color{};
    VulkanNativeImageFacts output{};
    VulkanQueueOwnership colorOwnership = VulkanQueueOwnership::Invalid;
    VulkanQueueOwnership outputOwnership = VulkanQueueOwnership::Invalid;
    VulkanTimelineContract producer{};
    VulkanTimelineContract consumer{};
    std::uint64_t recreationGeneration = 0;
    VulkanCarrierComposeIntent compose = VulkanCarrierComposeIntent::Invalid;
    bool externalInterop = false;
};

enum class VulkanCarrierExecutionFailure : std::uint8_t {
    None,
    InvalidFrame,
    InvalidWork,
    WorkFrameMismatch,
    WorkGenerationMismatch,
    WorkNotClaimed,
    InvalidRecreationGeneration,
    RecreationGenerationMismatch,
    ResourceIdentityMismatch,
    InvalidQueueFamily,
    InvalidLayout,
    InvalidUsage,
    InvalidDimensions,
    InvalidFormat,
    InvalidProvenance,
    InvalidOwnership,
    InvalidTimeline,
    InvalidComposeIntent
};

struct VulkanCarrierExecutionPlan {
    WorkTicket ticket{};
    std::uint64_t submissionEpoch = 0;
    std::uint64_t recreationGeneration = 0;
    std::uint64_t colorOpaqueId = 0;
    std::uint64_t outputOpaqueId = 0;
    Resolution colorResolution{};
    Resolution outputResolution{};
    ResourceFormat colorFormat = ResourceFormat::Unknown;
    ResourceFormat outputFormat = ResourceFormat::Unknown;
    std::uint32_t colorUsage = 0;
    std::uint32_t outputUsage = 0;
    VulkanImageLayoutIntent colorInitialLayout =
        VulkanImageLayoutIntent::Undefined;
    VulkanImageLayoutIntent outputInitialLayout =
        VulkanImageLayoutIntent::Undefined;
    VulkanImageLayoutIntent colorExecutionLayout =
        VulkanImageLayoutIntent::TransferSource;
    VulkanImageLayoutIntent outputExecutionLayout =
        VulkanImageLayoutIntent::TransferDestination;
    std::uint32_t queueFamilyIndex =
        std::numeric_limits<std::uint32_t>::max();
    VulkanQueueOwnership colorOwnership = VulkanQueueOwnership::Invalid;
    VulkanQueueOwnership outputOwnership = VulkanQueueOwnership::Invalid;
    VulkanTimelineContract producer{};
    VulkanTimelineContract consumer{};
    VulkanCarrierComposeIntent compose = VulkanCarrierComposeIntent::Invalid;
    ResourceProvenance colorProvenance = ResourceProvenance::Unknown;
    bool externalInterop = false;
    bool reset = false;
};

struct VulkanCarrierExecutionPlanResult {
    VulkanCarrierExecutionPlan plan{};
    VulkanCarrierExecutionFailure failure =
        VulkanCarrierExecutionFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == VulkanCarrierExecutionFailure::None;
    }
};

VulkanCarrierExecutionPlanResult BuildVulkanCarrierExecutionPlan(
    const VulkanCarrierSession& session,
    const VulkanCarrierFrameResult& frame,
    const VulkanCarrierWork& work,
    const VulkanCarrierExecutionResources& resources) noexcept;

} // namespace nrfusion
