#include "VulkanExternalInteropHarness.hpp"

namespace nrfusion::test {

bool VulkanExternalInteropHarness::CreatePrimaryCommandBuffer(
    VkCommandPool& pool,
    VkCommandBuffer& command) noexcept {
    pool = VK_NULL_HANDLE;
    command = VK_NULL_HANDLE;
    if (!device_ || !createCommandPool_ ||
        !allocateCommands_ || queueFamily_ == UINT32_MAX) {
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily_;
    poolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (createCommandPool_(
            device_, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo allocateInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (allocateCommands_(
            device_, &allocateInfo, &command) != VK_SUCCESS) {
        destroyCommandPool_(device_, pool, nullptr);
        pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void VulkanExternalInteropHarness::DestroyCommandPool(
    VkCommandPool pool) noexcept {
    if (device_ && destroyCommandPool_ && pool)
        destroyCommandPool_(device_, pool, nullptr);
}

bool VulkanExternalInteropHarness::Begin(
    VkCommandBuffer command) noexcept {
    if (!beginCommand_ || !command) return false;
    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return beginCommand_(command, &beginInfo) == VK_SUCCESS;
}

bool VulkanExternalInteropHarness::EndSubmitWaitSignal(
    VkCommandBuffer command,
    void* waitSemaphore,
    std::uint64_t waitValue,
    void* signalSemaphore,
    std::uint64_t signalValue) noexcept {
    if (!command || !endCommand_ ||
        !queue_ || !queueSubmit_ || !queueWaitIdle_ ||
        !waitSemaphore || !signalSemaphore ||
        waitValue == 0 || signalValue == 0) {
        return false;
    }
    if (endCommand_(command) != VK_SUCCESS) return false;

    VkSemaphore wait =
        reinterpret_cast<VkSemaphore>(waitSemaphore);
    VkSemaphore signal =
        reinterpret_cast<VkSemaphore>(signalSemaphore);
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkTimelineSemaphoreSubmitInfo timeline{
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    timeline.waitSemaphoreValueCount = 1;
    timeline.pWaitSemaphoreValues = &waitValue;
    timeline.signalSemaphoreValueCount = 1;
    timeline.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.pNext = &timeline;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &wait;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal;

    return queueSubmit_(
               queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           queueWaitIdle_(queue_) == VK_SUCCESS;
}

} // namespace nrfusion::test
