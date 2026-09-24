#include "nrfusion/SyntheticVulkanProvider.hpp"
#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {
namespace {

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
    if (!native.Valid() || !createImage || !destroyImage ||
        !getRequirements || !allocateMemory || !freeMemory ||
        !bindImageMemory || !getDeviceProcAddr) {
        return false;
    }

    VkExternalMemoryImageCreateInfo external{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext = &external;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = static_cast<VkFormat>(format);
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    if (createImage(native.device, &imageInfo, nullptr, &image) !=
        VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    getRequirements(native.device, image, &requirements);
    const auto memoryType = QueryD3D12ImportedMemoryType(
        native, getDeviceProcAddr, sharedHandle,
        requirements.memoryTypeBits);
    if (!memoryType) {
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
    import.handle = sharedHandle;

    VkMemoryAllocateInfo allocate{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.pNext = &import;
    allocate.allocationSize =
        allocationSize != 0 ? allocationSize : requirements.size;
    allocate.memoryTypeIndex = memoryType->memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (allocateMemory(
            native.device, &allocate, nullptr, &memory) != VK_SUCCESS) {
        destroyImage(native.device, image, nullptr);
        return false;
    }
    if (bindImageMemory(native.device, image, memory, 0) != VK_SUCCESS) {
        freeMemory(native.device, memory, nullptr);
        destroyImage(native.device, image, nullptr);
        return false;
    }

    outResource.vkImage = reinterpret_cast<void*>(image);
    outResource.vkMemory = reinterpret_cast<void*>(memory);
    outResource.d3d12Handle = sharedHandle;
    outResource.width = width;
    outResource.height = height;
    importedResources_.push_back(outResource);
    return true;
}

bool SyntheticVulkanProvider::ImportD3D12Fence(
    HANDLE sharedFenceHandle,
    ImportedVulkanSemaphore& outSemaphore) {
    if (!ready_ || !vk_.nativeResolved || !sharedFenceHandle)
        return false;

    const VulkanNativeContext native = NativeContext(
        vkInstance_, vkPhysicalDevice_, vkDevice_,
        vkQueue_, vkQueueFamilyIndex_);
    const auto createSemaphore =
        Proc<PFN_vkCreateSemaphore>(vk_.createSemaphore);
    const auto destroySemaphore =
        Proc<PFN_vkDestroySemaphore>(vk_.destroySemaphore);
    const auto importSemaphore =
        Proc<PFN_vkImportSemaphoreWin32HandleKHR>(
            vk_.importSemaphoreWin32Handle);
    if (!native.Valid() || !createSemaphore ||
        !destroySemaphore || !importSemaphore) {
        return false;
    }

    VkSemaphoreTypeCreateInfo typeInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

    VkSemaphoreCreateInfo createInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    createInfo.pNext = &typeInfo;

    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (createSemaphore(
            native.device, &createInfo, nullptr, &semaphore) !=
        VK_SUCCESS) {
        return false;
    }

    VkImportSemaphoreWin32HandleInfoKHR import{
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    import.semaphore = semaphore;
    import.handleType =
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    import.handle = sharedFenceHandle;
    if (importSemaphore(native.device, &import) != VK_SUCCESS) {
        destroySemaphore(native.device, semaphore, nullptr);
        return false;
    }

    outSemaphore.vkSemaphore = reinterpret_cast<void*>(semaphore);
    outSemaphore.d3d12FenceHandle = sharedFenceHandle;
    importedSemaphores_.push_back(outSemaphore);
    return true;
}

bool SyntheticVulkanProvider::WaitTimelineSemaphore(
    void* vkSemaphore, uint64_t value, uint32_t timeoutMs) {
    const auto waitSemaphores =
        Proc<PFN_vkWaitSemaphores>(vk_.waitSemaphores);
    if (!ready_ || !vk_.nativeResolved ||
        !vkDevice_ || !vkSemaphore || !waitSemaphores) {
        return false;
    }

    VkSemaphore semaphore =
        reinterpret_cast<VkSemaphore>(vkSemaphore);
    VkSemaphoreWaitInfo waitInfo{
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphore;
    waitInfo.pValues = &value;

    const uint64_t timeoutNs =
        static_cast<uint64_t>(timeoutMs) * 1000000ULL;
    return waitSemaphores(
        reinterpret_cast<VkDevice>(vkDevice_),
        &waitInfo, timeoutNs) == VK_SUCCESS;
}

uint64_t SyntheticVulkanProvider::QueryTimelineSemaphore(
    void* vkSemaphore) {
    const auto query =
        Proc<PFN_vkGetSemaphoreCounterValue>(
            vk_.getSemaphoreCounterValue);
    if (!ready_ || !vk_.nativeResolved ||
        !vkDevice_ || !vkSemaphore || !query) {
        return 0;
    }

    uint64_t value = 0;
    return query(
               reinterpret_cast<VkDevice>(vkDevice_),
               reinterpret_cast<VkSemaphore>(vkSemaphore),
               &value) == VK_SUCCESS
        ? value
        : 0;
}

} // namespace nrfusion
