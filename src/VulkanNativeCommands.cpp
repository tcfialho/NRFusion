#include "nrfusion/VulkanNativeCommands.hpp"

namespace nrfusion {
namespace {

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
    if (!dispatch.pipelineBarrier ||
        commandBuffer == VK_NULL_HANDLE ||
        image == VK_NULL_HANDLE) {
        return false;
    }

    VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
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

bool RecordVulkanCopyOrBlit(
    const VulkanCommandDispatch& dispatch,
    VkCommandBuffer commandBuffer,
    VkImage source,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    VkImage destination,
    uint32_t destinationWidth,
    uint32_t destinationHeight) noexcept {
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
            source, VK_IMAGE_LAYOUT_GENERAL,
            destination, VK_IMAGE_LAYOUT_GENERAL,
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
        source, VK_IMAGE_LAYOUT_GENERAL,
        destination, VK_IMAGE_LAYOUT_GENERAL,
        1, &region, VK_FILTER_LINEAR);
    return true;
}

} // namespace nrfusion
