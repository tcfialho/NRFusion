#pragma once

#include "nrfusion/VulkanNativeCommands.hpp"

#include <windows.h>

namespace nrfusion::test {

class VulkanNativeHarness {
public:
    VulkanNativeHarness() = default;
    ~VulkanNativeHarness();

    VulkanNativeHarness(const VulkanNativeHarness&) = delete;
    VulkanNativeHarness& operator=(const VulkanNativeHarness&) = delete;

    bool Open(bool hardwareRequired);
    void Close() noexcept;

    VulkanContextContract ContextContract() const noexcept;
    VulkanCommandDispatch CommandDispatch() const noexcept;

    bool CreateBoundImage(
        VkImage& image, VkDeviceMemory& memory) const noexcept;
    void DestroyBoundImage(
        VkImage image, VkDeviceMemory memory) const noexcept;

    bool CreatePrimaryCommandBuffer(
        VkCommandPool& pool,
        VkCommandBuffer& command) const noexcept;
    void DestroyCommandPool(VkCommandPool pool) const noexcept;

    bool Begin(VkCommandBuffer command) const noexcept;
    bool EndSubmitAndWait(VkCommandBuffer command) const noexcept;

private:
    template <typename T>
    T InstanceProc(const char* name) const noexcept {
        return reinterpret_cast<T>(
            getInstanceProc_(instance_, name));
    }

    template <typename T>
    T DeviceProc(const char* name) const noexcept {
        return reinterpret_cast<T>(
            getDeviceProc_(device_, name));
    }

    uint32_t SelectMemoryType(uint32_t bits) const noexcept;

    HMODULE loader_ = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProc_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProc_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = UINT32_MAX;
    VkPhysicalDeviceMemoryProperties memoryProperties_{};

    PFN_vkDestroyInstance destroyInstance_ = nullptr;
    PFN_vkDestroyDevice destroyDevice_ = nullptr;
    PFN_vkCreateImage createImage_ = nullptr;
    PFN_vkDestroyImage destroyImage_ = nullptr;
    PFN_vkGetImageMemoryRequirements getImageRequirements_ = nullptr;
    PFN_vkAllocateMemory allocateMemory_ = nullptr;
    PFN_vkFreeMemory freeMemory_ = nullptr;
    PFN_vkBindImageMemory bindImageMemory_ = nullptr;
    PFN_vkCreateCommandPool createCommandPool_ = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool_ = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommands_ = nullptr;
    PFN_vkBeginCommandBuffer beginCommand_ = nullptr;
    PFN_vkEndCommandBuffer endCommand_ = nullptr;
    PFN_vkQueueSubmit queueSubmit_ = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle_ = nullptr;
};

} // namespace nrfusion::test
