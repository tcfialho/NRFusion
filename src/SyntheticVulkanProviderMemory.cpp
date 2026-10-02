#include "nrfusion/SyntheticVulkanProvider.hpp"
#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {
namespace {

bool DuplicateForVulkanImport(
    HANDLE source, HANDLE& duplicate) noexcept {
    duplicate = nullptr;
    return source &&
        DuplicateHandle(
            GetCurrentProcess(), source,
            GetCurrentProcess(), &duplicate,
            0, FALSE, DUPLICATE_SAME_ACCESS) == TRUE;
}

template <typename T>
T Proc(FARPROC proc) noexcept {
    return reinterpret_cast<T>(proc);
}

VulkanNativeContext NativeContext(
    void* instance,
    void* physicalDevice,
    void* device,
    void* queue,
    uint32_t queueFamilyIndex) noexcept {
    VulkanContextContract contract{};
    contract.instance = reinterpret_cast<std::uintptr_t>(instance);
    contract.physicalDevice =
        reinterpret_cast<std::uintptr_t>(physicalDevice);
    contract.device = reinterpret_cast<std::uintptr_t>(device);
    contract.queue = reinterpret_cast<std::uintptr_t>(queue);
    contract.queueFamilyIndex = queueFamilyIndex;
    return MakeVulkanNativeContext(contract);
}

bool ExternalImageSupported(
    const VulkanNativeContext& native,
    PFN_vkGetPhysicalDeviceImageFormatProperties2 query,
    VkFormat format,
    VkImageUsageFlags usage) noexcept {
    if (!query) return false;

    VkPhysicalDeviceExternalImageFormatInfo externalInfo{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    externalInfo.handleType =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

    VkPhysicalDeviceImageFormatInfo2 formatInfo{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    formatInfo.pNext = &externalInfo;
    formatInfo.format = format;
    formatInfo.type = VK_IMAGE_TYPE_2D;
    formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    formatInfo.usage = usage;

    VkExternalImageFormatProperties externalProperties{
        VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkImageFormatProperties2 properties{
        VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    properties.pNext = &externalProperties;

    if (query(
            native.physicalDevice, &formatInfo, &properties) != VK_SUCCESS) {
        return false;
    }

    const auto& memory =
        externalProperties.externalMemoryProperties;
    return (memory.externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0 &&
        (memory.compatibleHandleTypes &
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT) != 0;
}

} // namespace

bool SyntheticVulkanProvider::ImportD3D12Resource(
    HANDLE sharedHandle,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint64_t allocationSize,
    ImportedVulkanResource& outResource) {
    if (!ready_ || !vk_.nativeResolved || !sharedHandle ||
        width == 0 || height == 0) {
        return false;
    }

    const VulkanNativeContext native = NativeContext(
        vkInstance_, vkPhysicalDevice_, vkDevice_,
        vkQueue_, vkQueueFamilyIndex_);
    const auto createImage = Proc<PFN_vkCreateImage>(vk_.createImage);
    const auto destroyImage = Proc<PFN_vkDestroyImage>(vk_.destroyImage);
    const auto getRequirements =
        Proc<PFN_vkGetImageMemoryRequirements>(
            vk_.getImageMemoryRequirements);
    const auto allocateMemory =
        Proc<PFN_vkAllocateMemory>(vk_.allocateMemory);
    const auto freeMemory = Proc<PFN_vkFreeMemory>(vk_.freeMemory);
    const auto bindImageMemory =
        Proc<PFN_vkBindImageMemory>(vk_.bindImageMemory);
    const auto getDeviceProcAddr =
        Proc<PFN_vkGetDeviceProcAddr>(vk_.getDeviceProcAddr);
    const auto queryFormat =
        Proc<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
            vk_.getPhysicalDeviceImageFormatProperties2);
    if (!native.Valid() || !createImage || !destroyImage ||
        !getRequirements || !allocateMemory || !freeMemory ||
        !bindImageMemory || !getDeviceProcAddr || !queryFormat) {
        return false;
    }

    const VkFormat vkFormat = static_cast<VkFormat>(format);
    const VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!ExternalImageSupported(
            native, queryFormat, vkFormat, usage)) {
        return false;
    }

    VkExternalMemoryImageCreateInfo external{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext = &external;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = vkFormat;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (createImage(native.device, &imageInfo, nullptr, &image) !=
        VK_SUCCESS) {
        return false;
    }

    HANDLE importHandle = nullptr;
    if (!DuplicateForVulkanImport(sharedHandle, importHandle)) {
        destroyImage(native.device, image, nullptr);
        return false;
    }

    VkMemoryRequirements requirements{};
    getRequirements(native.device, image, &requirements);
    const auto memoryType = QueryD3D12ImportedMemoryType(
        native, getDeviceProcAddr, importHandle,
        requirements.memoryTypeBits);
    if (!memoryType) {
        CloseHandle(importHandle);
        destroyImage(native.device, image, nullptr);
        return false;
    }

    VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = image;

    VkImportMemoryWin32HandleInfoKHR import{
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    import.pNext = &dedicated;
    import.handleType =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    import.handle = importHandle;

    if (allocationSize != 0 &&
        allocationSize < requirements.size) {
        CloseHandle(importHandle);
        destroyImage(native.device, image, nullptr);
        return false;
    }

    VkMemoryAllocateInfo allocate{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.pNext = &import;
    allocate.allocationSize =
        allocationSize != 0 ? allocationSize : requirements.size;
    allocate.memoryTypeIndex = memoryType->memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (allocateMemory(
            native.device, &allocate, nullptr, &memory) != VK_SUCCESS) {
        CloseHandle(importHandle);
        destroyImage(native.device, image, nullptr);
        return false;
    }
    // Vulkan retains the imported payload, but Win32 HANDLE ownership stays here.
    CloseHandle(importHandle);
    importHandle = nullptr;
    if (bindImageMemory(native.device, image, memory, 0) != VK_SUCCESS) {
        freeMemory(native.device, memory, nullptr);
        destroyImage(native.device, image, nullptr);
        return false;
    }

    outResource.vkImage = reinterpret_cast<void*>(image);
    outResource.vkMemory = reinterpret_cast<void*>(memory);
    outResource.width = width;
    outResource.height = height;
    importedResources_.push_back(outResource);
    return true;
}

} // namespace nrfusion
