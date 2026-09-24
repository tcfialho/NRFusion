#include "VulkanExternalInteropHarness.hpp"

#include <cstring>
#include <vector>

namespace nrfusion::test {
namespace {

bool HasExtension(
    const std::vector<VkExtensionProperties>& extensions,
    const char* name) {
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0)
            return true;
    }
    return false;
}

bool LuidMatches(
    PFN_vkGetPhysicalDeviceProperties2 getProperties2,
    VkPhysicalDevice device,
    const LUID& requiredLuid,
    bool hardwareRequired) {
    VkPhysicalDeviceIDProperties id{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &id;
    getProperties2(device, &properties);

    if (hardwareRequired &&
        properties.properties.deviceType ==
            VK_PHYSICAL_DEVICE_TYPE_CPU) {
        return false;
    }

    return id.deviceLUIDValid &&
        std::memcmp(
            id.deviceLUID, &requiredLuid,
            VK_LUID_SIZE) == 0;
}

} // namespace

VulkanExternalInteropHarness::~VulkanExternalInteropHarness() {
    Close();
}

bool VulkanExternalInteropHarness::Open(
    const LUID& adapterLuid,
    bool hardwareRequired) {
    loader_ = LoadLibraryW(L"vulkan-1.dll");
    if (!loader_) return false;

    getInstanceProc_ =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(loader_, "vkGetInstanceProcAddr"));
    if (!getInstanceProc_) return false;

    const auto createInstance =
        reinterpret_cast<PFN_vkCreateInstance>(
            getInstanceProc_(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!createInstance) return false;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NRFusion external interop harness";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    if (createInstance(
            &instanceInfo, nullptr, &instance_) != VK_SUCCESS) {
        return false;
    }

    destroyInstance_ =
        InstanceProc<PFN_vkDestroyInstance>("vkDestroyInstance");
    const auto enumerateDevices =
        InstanceProc<PFN_vkEnumeratePhysicalDevices>(
            "vkEnumeratePhysicalDevices");
    const auto getProperties2 =
        InstanceProc<PFN_vkGetPhysicalDeviceProperties2>(
            "vkGetPhysicalDeviceProperties2");
    const auto getQueueProperties =
        InstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto enumerateExtensions =
        InstanceProc<PFN_vkEnumerateDeviceExtensionProperties>(
            "vkEnumerateDeviceExtensionProperties");
    const auto getFeatures2 =
        InstanceProc<PFN_vkGetPhysicalDeviceFeatures2>(
            "vkGetPhysicalDeviceFeatures2");
    const auto createDevice =
        InstanceProc<PFN_vkCreateDevice>("vkCreateDevice");
    getDeviceProc_ =
        InstanceProc<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    if (!destroyInstance_ || !enumerateDevices || !getProperties2 ||
        !getQueueProperties || !enumerateExtensions ||
        !getFeatures2 || !createDevice || !getDeviceProc_) {
        return false;
    }

    uint32_t deviceCount = 0;
    if (enumerateDevices(
            instance_, &deviceCount, nullptr) != VK_SUCCESS ||
        deviceCount == 0) {
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (enumerateDevices(
            instance_, &deviceCount, devices.data()) != VK_SUCCESS) {
        return false;
    }

    for (VkPhysicalDevice candidate : devices) {
        if (!LuidMatches(
                getProperties2, candidate,
                adapterLuid, hardwareRequired)) {
            continue;
        }

        uint32_t extensionCount = 0;
        if (enumerateExtensions(
                candidate, nullptr,
                &extensionCount, nullptr) != VK_SUCCESS) {
            continue;
        }
        std::vector<VkExtensionProperties> extensions(extensionCount);
        if (enumerateExtensions(
                candidate, nullptr,
                &extensionCount, extensions.data()) != VK_SUCCESS) {
            continue;
        }
        if (!HasExtension(
                extensions,
                VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) ||
            !HasExtension(
                extensions,
                VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) ||
            !HasExtension(
                extensions,
                VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
            continue;
        }

        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        VkPhysicalDeviceFeatures2 features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &timeline;
        getFeatures2(candidate, &features);
        if (!timeline.timelineSemaphore) continue;

        uint32_t queueCount = 0;
        getQueueProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        getQueueProperties(candidate, &queueCount, queues.data());
        for (uint32_t index = 0; index != queueCount; ++index) {
            if ((queues[index].queueFlags &
                 VK_QUEUE_GRAPHICS_BIT) != 0) {
                physicalDevice_ = candidate;
                queueFamily_ = index;
                break;
            }
        }
        if (physicalDevice_ != VK_NULL_HANDLE) break;
    }
    if (physicalDevice_ == VK_NULL_HANDLE) return false;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore = VK_TRUE;

    const char* extensions[] = {
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME};

    VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &timeline;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount =
        static_cast<uint32_t>(std::size(extensions));
    deviceInfo.ppEnabledExtensionNames = extensions;
    if (createDevice(
            physicalDevice_, &deviceInfo,
            nullptr, &device_) != VK_SUCCESS) {
        return false;
    }

    destroyDevice_ =
        DeviceProc<PFN_vkDestroyDevice>("vkDestroyDevice");
    const auto getDeviceQueue =
        DeviceProc<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
    queueSubmit_ = DeviceProc<PFN_vkQueueSubmit>("vkQueueSubmit");
    queueWaitIdle_ =
        DeviceProc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle");
    if (!destroyDevice_ || !getDeviceQueue ||
        !queueSubmit_ || !queueWaitIdle_) {
        return false;
    }

    getDeviceQueue(device_, queueFamily_, 0, &queue_);
    return queue_ != VK_NULL_HANDLE;
}

void VulkanExternalInteropHarness::Close() noexcept {
    if (device_ && queueWaitIdle_ && queue_)
        queueWaitIdle_(queue_);
    if (device_ && destroyDevice_)
        destroyDevice_(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;

    if (instance_ && destroyInstance_)
        destroyInstance_(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    queueFamily_ = UINT32_MAX;

    if (loader_) FreeLibrary(loader_);
    loader_ = nullptr;
    getInstanceProc_ = nullptr;
    getDeviceProc_ = nullptr;
}

ProviderContext VulkanExternalInteropHarness::Context() const noexcept {
    ProviderContext context{};
    context.api = GraphicsApi::Vulkan;
    context.instance = instance_;
    context.physicalDevice = physicalDevice_;
    context.device = device_;
    context.commandQueue = queue_;
    context.queueFamilyIndex = queueFamily_;
    return context;
}

bool VulkanExternalInteropHarness::SubmitWaitSignal(
    void* waitSemaphore,
    std::uint64_t waitValue,
    void* signalSemaphore,
    std::uint64_t signalValue) noexcept {
    if (!queue_ || !queueSubmit_ || !queueWaitIdle_ ||
        !waitSemaphore || !signalSemaphore ||
        waitValue == 0 || signalValue == 0) {
        return false;
    }

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
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal;

    return queueSubmit_(
               queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           queueWaitIdle_(queue_) == VK_SUCCESS;
}

} // namespace nrfusion::test
