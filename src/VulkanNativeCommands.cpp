#include "nrfusion/VulkanNativeCommands.hpp"

namespace nrfusion {
namespace {

bool RecordOwnershipBarrier(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags sourceAccess,
    VkAccessFlags destinationAccess,
    uint32_t sourceQueueFamily,
    uint32_t destinationQueueFamily) noexcept {
    if (!dispatch.pipelineBarrier ||
        commandBuffer == VK_NULL_HANDLE ||
        image == VK_NULL_HANDLE) {
        return false;
    }

    VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    dispatch.pipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    return true;
}

VkImageSubresourceLayers ColorLayers() noexcept {
    VkImageSubresourceLayers layers{};
    layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layers.layerCount = 1;
    return layers;
}

} // namespace

bool RecordVulkanImageTransition(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t sourceQueueFamily,
    uint32_t destinationQueueFamily) noexcept {
    return RecordOwnershipBarrier(
        dispatch, commandBuffer, image,
        oldLayout, newLayout,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        sourceQueueFamily, destinationQueueFamily);
}

bool RecordVulkanExternalImageAcquire(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout externalLayout,
    VkImageLayout localLayout,
    uint32_t localQueueFamily) noexcept {
    return RecordOwnershipBarrier(
        dispatch, commandBuffer, image,
        externalLayout, localLayout,
        0,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        VK_QUEUE_FAMILY_EXTERNAL, localQueueFamily);
}

bool RecordVulkanExternalImageRelease(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout localLayout,
    VkImageLayout externalLayout,
    uint32_t localQueueFamily) noexcept {
    return RecordOwnershipBarrier(
        dispatch, commandBuffer, image,
        localLayout, externalLayout,
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        0,
        localQueueFamily, VK_QUEUE_FAMILY_EXTERNAL);
}

bool RecordVulkanCopyOrBlit(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage source,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    VkImage destination,
    uint32_t destinationWidth,
    uint32_t destinationHeight,
    VkImageLayout sourceLayout,
    VkImageLayout destinationLayout) noexcept {
    if (!dispatch.Valid() ||
        commandBuffer == VK_NULL_HANDLE ||
        source == VK_NULL_HANDLE ||
        destination == VK_NULL_HANDLE ||
        sourceWidth == 0 || sourceHeight == 0 ||
        destinationWidth == 0 || destinationHeight == 0) {
        return false;
    }

    if (sourceWidth == destinationWidth &&
        sourceHeight == destinationHeight &&
        dispatch.copyImage) {
        VkImageCopy region{};
        region.srcSubresource = ColorLayers();
        region.dstSubresource = ColorLayers();
        region.extent = {sourceWidth, sourceHeight, 1};
        dispatch.copyImage(
            commandBuffer,
            source, sourceLayout,
            destination, destinationLayout,
            1, &region);
        return true;
    }

    if (!dispatch.blitImage) return false;

    VkImageBlit region{};
    region.srcSubresource = ColorLayers();
    region.dstSubresource = ColorLayers();
    region.srcOffsets[1] = {
        static_cast<int32_t>(sourceWidth),
        static_cast<int32_t>(sourceHeight), 1};
    region.dstOffsets[1] = {
        static_cast<int32_t>(destinationWidth),
        static_cast<int32_t>(destinationHeight), 1};
    dispatch.blitImage(
        commandBuffer,
        source, sourceLayout,
        destination, destinationLayout,
        1, &region, VK_FILTER_LINEAR);
    return true;
}

} // namespace nrfusion
