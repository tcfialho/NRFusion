#include "nrfusion/SyntheticVulkanProvider.hpp"
#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {
namespace {

template <typename T>
T Proc(FARPROC proc) noexcept {
    return reinterpret_cast<T>(proc);
}

VkImageSubresourceLayers ColorLayers() noexcept {
    VkImageSubresourceLayers layers{};
    layers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    layers.layerCount = 1;
    return layers;
}

} // namespace

bool SyntheticVulkanProvider::TransitionImageLayout(
    void* cmdBuffer,
    void* image,
    uint32_t oldLayout,
    uint32_t newLayout) {
    const auto pipelineBarrier =
        Proc<PFN_vkCmdPipelineBarrier>(vk_.cmdPipelineBarrier);
    if (!ready_ || !vk_.nativeResolved ||
        !cmdBuffer || !image || !pipelineBarrier) {
        return false;
    }

    VkImageMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout = static_cast<VkImageLayout>(oldLayout);
    barrier.newLayout = static_cast<VkImageLayout>(newLayout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = reinterpret_cast<VkImage>(image);
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    pipelineBarrier(
        reinterpret_cast<VkCommandBuffer>(cmdBuffer),
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    return true;
}

bool SyntheticVulkanProvider::BlitOrCopy(
    void* cmdBuffer,
    void* srcImage,
    uint32_t srcWidth,
    uint32_t srcHeight,
    void* dstImage,
    uint32_t dstWidth,
    uint32_t dstHeight) {
    if (!ready_ || !vk_.nativeResolved ||
        !cmdBuffer || !srcImage || !dstImage ||
        srcWidth == 0 || srcHeight == 0 ||
        dstWidth == 0 || dstHeight == 0) {
        return false;
    }

    const auto copyImage =
        Proc<PFN_vkCmdCopyImage>(vk_.cmdCopyImage);
    const auto blitImage =
        Proc<PFN_vkCmdBlitImage>(vk_.cmdBlitImage);
    const VkCommandBuffer commandBuffer =
        reinterpret_cast<VkCommandBuffer>(cmdBuffer);
    const VkImage source = reinterpret_cast<VkImage>(srcImage);
    const VkImage destination = reinterpret_cast<VkImage>(dstImage);

    if (srcWidth == dstWidth && srcHeight == dstHeight && copyImage) {
        VkImageCopy region{};
        region.srcSubresource = ColorLayers();
        region.dstSubresource = ColorLayers();
        region.extent = {srcWidth, srcHeight, 1};
        copyImage(
            commandBuffer,
            source, VK_IMAGE_LAYOUT_GENERAL,
            destination, VK_IMAGE_LAYOUT_GENERAL,
            1, &region);
        return true;
    }

    if (!blitImage) return false;

    VkImageBlit region{};
    region.srcSubresource = ColorLayers();
    region.dstSubresource = ColorLayers();
    region.srcOffsets[1] = {
        static_cast<int32_t>(srcWidth),
        static_cast<int32_t>(srcHeight),
        1};
    region.dstOffsets[1] = {
        static_cast<int32_t>(dstWidth),
        static_cast<int32_t>(dstHeight),
        1};
    blitImage(
        commandBuffer,
        source, VK_IMAGE_LAYOUT_GENERAL,
        destination, VK_IMAGE_LAYOUT_GENERAL,
        1, &region, VK_FILTER_LINEAR);
    return true;
}

} // namespace nrfusion
