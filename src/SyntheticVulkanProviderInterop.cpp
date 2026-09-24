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

    HANDLE importHandle = nullptr;
    if (!DuplicateForVulkanImport(sharedFenceHandle, importHandle))
        return false;

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
        CloseHandle(importHandle);
        return false;
    }

    VkImportSemaphoreWin32HandleInfoKHR import{
        VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR};
    import.semaphore = semaphore;
    import.handleType =
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
    import.handle = importHandle;
    if (importSemaphore(native.device, &import) != VK_SUCCESS) {
        CloseHandle(importHandle);
        destroySemaphore(native.device, semaphore, nullptr);
        return false;
    }
    // Vulkan retains the imported payload, but Win32 HANDLE ownership stays here.
    CloseHandle(importHandle);
    importHandle = nullptr;

    outSemaphore.vkSemaphore = reinterpret_cast<void*>(semaphore);
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
