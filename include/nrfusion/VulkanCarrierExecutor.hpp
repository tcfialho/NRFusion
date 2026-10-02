#pragma once

#include "nrfusion/VulkanCarrierExecutionPlan.hpp"
#include "nrfusion/VulkanNativeCommands.hpp"

namespace nrfusion {

struct VulkanCarrierExecutionContext {
    VulkanNativeContext native{};
    VulkanCommandDispatch commands{};
};

struct VulkanCarrierNativeResources {
    VulkanCarrierExecutionResources facts{};
    VkImage color = VK_NULL_HANDLE;
    VkImage output = VK_NULL_HANDLE;
};

enum class VulkanCarrierExecuteFailure : std::uint8_t {
    None,
    Planning,
    InvalidContext,
    InvalidCommandBuffer,
    InvalidDispatch,
    MissingImage,
    QueueFamilyMismatch,
    ClaimUnavailable,
    RecordingFailed
};

struct VulkanCarrierExecuteResult {
    VulkanCarrierExecutionPlan plan{};
    VulkanCarrierExecuteFailure failure =
        VulkanCarrierExecuteFailure::None;
    VulkanCarrierExecutionFailure planningFailure =
        VulkanCarrierExecutionFailure::None;
    bool attempted = false;
    bool claimConsumed = false;

    constexpr bool NeedsSubmission() const noexcept {
        return attempted &&
               claimConsumed &&
               failure == VulkanCarrierExecuteFailure::None;
    }

    constexpr explicit operator bool() const noexcept {
        return NeedsSubmission();
    }
};

class VulkanCarrierExecutor {
public:
    VulkanCarrierExecuteResult Execute(
        VkCommandBuffer commandBuffer,
        VulkanCarrierSession& session,
        const VulkanCarrierExecutionContext& context,
        const VulkanCarrierFrameResult& frame,
        const VulkanCarrierWork& work,
        const VulkanCarrierNativeResources& resources) const noexcept;
};

} // namespace nrfusion
