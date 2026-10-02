#include "nrfusion/SyntheticVulkanProvider.hpp"
#include "nrfusion/VulkanNativeContext.hpp"

namespace nrfusion {
namespace {

template <typename T>
T Proc(FARPROC proc) noexcept {
    return reinterpret_cast<T>(proc);
}

FARPROC Resolve(
    PFN_vkGetDeviceProcAddr getDeviceProcAddr,
    VkDevice device,
    const char* name) noexcept {
    return reinterpret_cast<FARPROC>(
        getDeviceProcAddr(device, name));
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

bool SyntheticVulkanProvider::InitializeNativeVulkan(
    const ProviderContext& context) {
    VulkanContextContract contract{};
    contract.instance = reinterpret_cast<std::uintptr_t>(context.instance);
    contract.physicalDevice =
        reinterpret_cast<std::uintptr_t>(context.physicalDevice);
    contract.device = reinterpret_cast<std::uintptr_t>(context.device);
    contract.queue = reinterpret_cast<std::uintptr_t>(context.commandQueue);
    contract.queueFamilyIndex = context.queueFamilyIndex;
    if (!ValidateVulkanContextContract(contract)) return false;

    const VulkanNativeContext native = MakeVulkanNativeContext(contract);
    const auto getInstanceProcAddr =
        Proc<PFN_vkGetInstanceProcAddr>(vk_.getInstanceProcAddr);
    const auto getDeviceProcAddr =
        Proc<PFN_vkGetDeviceProcAddr>(vk_.getDeviceProcAddr);
    if (!native.Valid() || !getInstanceProcAddr || !getDeviceProcAddr)
        return false;

    vkInstance_ = context.instance;
    vkPhysicalDevice_ = context.physicalDevice;
    vkDevice_ = context.device;
    vkQueue_ = context.commandQueue;
    vkQueueFamilyIndex_ = context.queueFamilyIndex;

    vk_.getPhysicalDeviceImageFormatProperties2 =
        reinterpret_cast<FARPROC>(
            getInstanceProcAddr(
                native.instance,
                "vkGetPhysicalDeviceImageFormatProperties2"));
    if (!vk_.getPhysicalDeviceImageFormatProperties2) {
        vk_.getPhysicalDeviceImageFormatProperties2 =
            reinterpret_cast<FARPROC>(
                getInstanceProcAddr(
                    native.instance,
                    "vkGetPhysicalDeviceImageFormatProperties2KHR"));
    }

    vk_.getPhysicalDeviceExternalSemaphoreProperties =
        reinterpret_cast<FARPROC>(
            getInstanceProcAddr(
                native.instance,
                "vkGetPhysicalDeviceExternalSemaphoreProperties"));
    if (!vk_.getPhysicalDeviceExternalSemaphoreProperties) {
        vk_.getPhysicalDeviceExternalSemaphoreProperties =
            reinterpret_cast<FARPROC>(
                getInstanceProcAddr(
                    native.instance,
                    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    }

    vk_.createSemaphore = Resolve(
        getDeviceProcAddr, native.device, "vkCreateSemaphore");
    vk_.destroySemaphore = Resolve(
        getDeviceProcAddr, native.device, "vkDestroySemaphore");
    vk_.importSemaphoreWin32Handle = Resolve(
        getDeviceProcAddr, native.device,
        "vkImportSemaphoreWin32HandleKHR");
    vk_.createImage = Resolve(
        getDeviceProcAddr, native.device, "vkCreateImage");
    vk_.destroyImage = Resolve(
        getDeviceProcAddr, native.device, "vkDestroyImage");
    vk_.getImageMemoryRequirements = Resolve(
        getDeviceProcAddr, native.device,
        "vkGetImageMemoryRequirements");
    vk_.allocateMemory = Resolve(
        getDeviceProcAddr, native.device, "vkAllocateMemory");
    vk_.freeMemory = Resolve(
        getDeviceProcAddr, native.device, "vkFreeMemory");
    vk_.bindImageMemory = Resolve(
        getDeviceProcAddr, native.device, "vkBindImageMemory");
    vk_.waitSemaphores = Resolve(
        getDeviceProcAddr, native.device, "vkWaitSemaphores");
    if (!vk_.waitSemaphores) {
        vk_.waitSemaphores = Resolve(
            getDeviceProcAddr, native.device, "vkWaitSemaphoresKHR");
    }
    vk_.getSemaphoreCounterValue = Resolve(
        getDeviceProcAddr, native.device, "vkGetSemaphoreCounterValue");
    if (!vk_.getSemaphoreCounterValue) {
        vk_.getSemaphoreCounterValue = Resolve(
            getDeviceProcAddr, native.device,
            "vkGetSemaphoreCounterValueKHR");
    }
    vk_.cmdPipelineBarrier = Resolve(
        getDeviceProcAddr, native.device, "vkCmdPipelineBarrier");
    vk_.cmdCopyImage = Resolve(
        getDeviceProcAddr, native.device, "vkCmdCopyImage");
    vk_.cmdBlitImage = Resolve(
        getDeviceProcAddr, native.device, "vkCmdBlitImage");

    vk_.nativeResolved =
        vk_.getPhysicalDeviceImageFormatProperties2 &&
        vk_.getPhysicalDeviceExternalSemaphoreProperties &&
        vk_.createSemaphore &&
        vk_.destroySemaphore &&
        vk_.importSemaphoreWin32Handle &&
        vk_.createImage &&
        vk_.destroyImage &&
        vk_.getImageMemoryRequirements &&
        vk_.allocateMemory &&
        vk_.freeMemory &&
        vk_.bindImageMemory &&
        vk_.waitSemaphores &&
        vk_.getSemaphoreCounterValue &&
        vk_.cmdPipelineBarrier &&
        (vk_.cmdCopyImage || vk_.cmdBlitImage);
    return vk_.nativeResolved;
}

void SyntheticVulkanProvider::ShutdownNativeVulkan() noexcept {
    const VulkanNativeContext native = NativeContext(
        vkInstance_, vkPhysicalDevice_, vkDevice_,
        vkQueue_, vkQueueFamilyIndex_);
    const auto destroySemaphore =
        Proc<PFN_vkDestroySemaphore>(vk_.destroySemaphore);
    const auto destroyImage =
        Proc<PFN_vkDestroyImage>(vk_.destroyImage);
    const auto freeMemory =
        Proc<PFN_vkFreeMemory>(vk_.freeMemory);

    if (native.Valid()) {
        for (auto& semaphore : importedSemaphores_) {
            if (semaphore.vkSemaphore && destroySemaphore) {
                destroySemaphore(
                    native.device,
                    reinterpret_cast<VkSemaphore>(
                        semaphore.vkSemaphore),
                    nullptr);
            }
        }
        for (auto& resource : importedResources_) {
            if (resource.vkImage && destroyImage) {
                destroyImage(
                    native.device,
                    reinterpret_cast<VkImage>(resource.vkImage),
                    nullptr);
            }
            if (resource.vkMemory && freeMemory) {
                freeMemory(
                    native.device,
                    reinterpret_cast<VkDeviceMemory>(
                        resource.vkMemory),
                    nullptr);
            }
        }
    }

    importedSemaphores_.clear();
    importedResources_.clear();
    vkInstance_ = nullptr;
    vkPhysicalDevice_ = nullptr;
    vkDevice_ = nullptr;
    vkQueue_ = nullptr;
    vkQueueFamilyIndex_ = UINT32_MAX;
    vk_.nativeResolved = false;
}

} // namespace nrfusion
