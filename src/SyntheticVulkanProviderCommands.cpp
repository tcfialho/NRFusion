#include "nrfusion/SyntheticVulkanProvider.hpp"
#include "nrfusion/VulkanNativeCommands.hpp"

namespace nrfusion {
namespace {

template <typename T>
T Proc(FARPROC proc) noexcept {
    return reinterpret_cast<T>(proc);
}

VulkanCommandDispatch Dispatch(
    const VulkanRawDispatch& raw) noexcept {
    VulkanCommandDispatch dispatch{};
    dispatch.pipelineBarrier =
        Proc<PFN_vkCmdPipelineBarrier>(raw.cmdPipelineBarrier);
    dispatch.copyImage =
        Proc<PFN_vkCmdCopyImage>(raw.cmdCopyImage);
    dispatch.blitImage =
        Proc<PFN_vkCmdBlitImage>(raw.cmdBlitImage);
    return dispatch;
}

} // namespace

bool SyntheticVulkanProvider::TransitionImageLayout(
    void* cmdBuffer,
    void* image,
    uint32_t oldLayout,
    uint32_t newLayout) {
    if (!ready_ || !vk_.nativeResolved) return false;
    return RecordVulkanImageTransition(
        Dispatch(vk_),
        reinterpret_cast<VkCommandBuffer>(cmdBuffer),
        reinterpret_cast<VkImage>(image),
        static_cast<VkImageLayout>(oldLayout),
        static_cast<VkImageLayout>(newLayout));
}

bool SyntheticVulkanProvider::BlitOrCopy(
    void* cmdBuffer,
    void* srcImage,
    uint32_t srcWidth,
    uint32_t srcHeight,
    void* dstImage,
    uint32_t dstWidth,
    uint32_t dstHeight) {
    if (!ready_ || !vk_.nativeResolved) return false;
    return RecordVulkanCopyOrBlit(
        Dispatch(vk_),
        reinterpret_cast<VkCommandBuffer>(cmdBuffer),
        reinterpret_cast<VkImage>(srcImage),
        srcWidth, srcHeight,
        reinterpret_cast<VkImage>(dstImage),
        dstWidth, dstHeight);
}

} // namespace nrfusion
