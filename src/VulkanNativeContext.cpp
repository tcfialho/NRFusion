#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {

VulkanNativeContext MakeVulkanNativeContext(
    const VulkanContextContract& context) noexcept {
    VulkanNativeContext native{};
    if (!context.Valid()) return native;

    native.instance = reinterpret_cast<VkInstance>(context.instance);
    native.physicalDevice =
        reinterpret_cast<VkPhysicalDevice>(context.physicalDevice);
    native.device = reinterpret_cast<VkDevice>(context.device);
    native.queue = reinterpret_cast<VkQueue>(context.queue);
    native.queueFamilyIndex = context.queueFamilyIndex;
    return native;
}

std::optional<VulkanImportedMemoryType> QueryD3D12ImportedMemoryType(
    const VulkanNativeContext& context,
    PFN_vkGetDeviceProcAddr getDeviceProcAddr,
    HANDLE sharedHandle,
    std::uint32_t imageMemoryTypeBits) noexcept {
    if (!context.Valid() || !getDeviceProcAddr || !sharedHandle ||
        imageMemoryTypeBits == 0) {
        return std::nullopt;
    }

    const auto query =
        reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
            getDeviceProcAddr(
                context.device, "vkGetMemoryWin32HandlePropertiesKHR"));
    if (!query) return std::nullopt;

    VkMemoryWin32HandlePropertiesKHR properties{
        VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if (query(
            context.device,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
            sharedHandle,
            &properties) != VK_SUCCESS) {
        return std::nullopt;
    }

    const auto index = SelectVulkanMemoryTypeIndex(
        imageMemoryTypeBits, properties.memoryTypeBits);
    if (!index) return std::nullopt;

    return VulkanImportedMemoryType{properties.memoryTypeBits, *index};
}

} // namespace nrfusion
