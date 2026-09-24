#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "nrfusion/VulkanCarrierContract.hpp"

#include <optional>

namespace nrfusion {

struct VulkanNativeContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    bool Valid() const noexcept {
        return instance != VK_NULL_HANDLE &&
               physicalDevice != VK_NULL_HANDLE &&
               device != VK_NULL_HANDLE &&
               queue != VK_NULL_HANDLE &&
               queueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
    }
};

struct VulkanImportedMemoryType {
    std::uint32_t memoryTypeBits = 0;
    std::uint32_t memoryTypeIndex = 0;
};

VulkanNativeContext MakeVulkanNativeContext(
    const VulkanContextContract& context) noexcept;

std::optional<VulkanImportedMemoryType> QueryD3D12ImportedMemoryType(
    const VulkanNativeContext& context,
    PFN_vkGetDeviceProcAddr getDeviceProcAddr,
    HANDLE sharedHandle,
    std::uint32_t imageMemoryTypeBits) noexcept;

} // namespace nrfusion
