#pragma once

#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {

struct VulkanCommandDispatch {
    PFN_vkCmdPipelineBarrier pipelineBarrier = nullptr;
    PFN_vkCmdCopyImage copyImage = nullptr;
    PFN_vkCmdBlitImage blitImage = nullptr;

    bool Valid() const noexcept {
        return pipelineBarrier && (copyImage || blitImage);
    }
};

bool RecordVulkanImageTransition(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t sourceQueueFamily = VK_QUEUE_FAMILY_IGNORED,
    uint32_t destinationQueueFamily = VK_QUEUE_FAMILY_IGNORED) noexcept;

bool RecordVulkanExternalImageAcquire(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout externalLayout,
    VkImageLayout localLayout,
    uint32_t localQueueFamily) noexcept;

bool RecordVulkanExternalImageRelease(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout localLayout,
    VkImageLayout externalLayout,
    uint32_t localQueueFamily) noexcept;

bool RecordVulkanCopyOrBlit(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage source,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    VkImage destination,
    uint32_t destinationWidth,
    uint32_t destinationHeight) noexcept;

} // namespace nrfusion
