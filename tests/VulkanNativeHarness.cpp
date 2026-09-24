#include "VulkanNativeHarness.hpp"

#include <vector>

namespace nrfusion::test {

VulkanNativeHarness::~VulkanNativeHarness() {
    Close();
}

bool VulkanNativeHarness::Open(bool hardwareRequired) {
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
    app.pApplicationName = "NRFusion Vulkan native harness";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    if (createInstance(&instanceInfo, nullptr, &instance_) != VK_SUCCESS)
        return false;

    destroyInstance_ = InstanceProc<PFN_vkDestroyInstance>(
        "vkDestroyInstance");
    const auto enumerateDevices =
        InstanceProc<PFN_vkEnumeratePhysicalDevices>(
            "vkEnumeratePhysicalDevices");
    const auto getQueueProperties =
        InstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto getDeviceProperties =
        InstanceProc<PFN_vkGetPhysicalDeviceProperties>(
            "vkGetPhysicalDeviceProperties");
    const auto getMemoryProperties =
        InstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
            "vkGetPhysicalDeviceMemoryProperties");
    const auto createDevice =
        InstanceProc<PFN_vkCreateDevice>("vkCreateDevice");
    getDeviceProc_ =
        InstanceProc<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
    if (!destroyInstance_ || !enumerateDevices ||
        !getQueueProperties || !getDeviceProperties ||
        !getMemoryProperties || !createDevice || !getDeviceProc_) {
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
        VkPhysicalDeviceProperties properties{};
        getDeviceProperties(candidate, &properties);
        if (hardwareRequired &&
            properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            continue;
        }

        uint32_t queueCount = 0;
        getQueueProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        getQueueProperties(candidate, &queueCount, queues.data());
        for (uint32_t index = 0; index != queueCount; ++index) {
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
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
    VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (createDevice(
            physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
        return false;
    }

    destroyDevice_ = DeviceProc<PFN_vkDestroyDevice>("vkDestroyDevice");
    const auto getDeviceQueue =
        DeviceProc<PFN_vkGetDeviceQueue>("vkGetDeviceQueue");
    createImage_ = DeviceProc<PFN_vkCreateImage>("vkCreateImage");
    destroyImage_ = DeviceProc<PFN_vkDestroyImage>("vkDestroyImage");
    getImageRequirements_ =
        DeviceProc<PFN_vkGetImageMemoryRequirements>(
            "vkGetImageMemoryRequirements");
    allocateMemory_ =
        DeviceProc<PFN_vkAllocateMemory>("vkAllocateMemory");
    freeMemory_ = DeviceProc<PFN_vkFreeMemory>("vkFreeMemory");
    bindImageMemory_ =
        DeviceProc<PFN_vkBindImageMemory>("vkBindImageMemory");
    createCommandPool_ =
        DeviceProc<PFN_vkCreateCommandPool>("vkCreateCommandPool");
    destroyCommandPool_ =
        DeviceProc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool");
    allocateCommands_ =
        DeviceProc<PFN_vkAllocateCommandBuffers>(
            "vkAllocateCommandBuffers");
    beginCommand_ =
        DeviceProc<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer");
    endCommand_ =
        DeviceProc<PFN_vkEndCommandBuffer>("vkEndCommandBuffer");
    queueSubmit_ = DeviceProc<PFN_vkQueueSubmit>("vkQueueSubmit");
    queueWaitIdle_ =
        DeviceProc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle");
    if (!destroyDevice_ || !getDeviceQueue || !createImage_ ||
        !destroyImage_ || !getImageRequirements_ ||
        !allocateMemory_ || !freeMemory_ || !bindImageMemory_ ||
        !createCommandPool_ || !destroyCommandPool_ ||
        !allocateCommands_ || !beginCommand_ || !endCommand_ ||
        !queueSubmit_ || !queueWaitIdle_) {
        return false;
    }

    getDeviceQueue(device_, queueFamily_, 0, &queue_);
    getMemoryProperties(physicalDevice_, &memoryProperties_);
    return queue_ != VK_NULL_HANDLE;
}

void VulkanNativeHarness::Close() noexcept {
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

VulkanContextContract
VulkanNativeHarness::ContextContract() const noexcept {
    VulkanContextContract contract{};
    contract.instance =
        reinterpret_cast<std::uintptr_t>(instance_);
    contract.physicalDevice =
        reinterpret_cast<std::uintptr_t>(physicalDevice_);
    contract.device =
        reinterpret_cast<std::uintptr_t>(device_);
    contract.queue =
        reinterpret_cast<std::uintptr_t>(queue_);
    contract.queueFamilyIndex = queueFamily_;
    return contract;
}

VulkanCommandDispatch
VulkanNativeHarness::CommandDispatch() const noexcept {
    VulkanCommandDispatch dispatch{};
    dispatch.pipelineBarrier =
        DeviceProc<PFN_vkCmdPipelineBarrier>(
            "vkCmdPipelineBarrier");
    dispatch.copyImage =
        DeviceProc<PFN_vkCmdCopyImage>("vkCmdCopyImage");
    dispatch.blitImage =
        DeviceProc<PFN_vkCmdBlitImage>("vkCmdBlitImage");
    return dispatch;
}

uint32_t VulkanNativeHarness::SelectMemoryType(
    uint32_t bits) const noexcept {
    for (uint32_t i = 0; i != memoryProperties_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 &&
            (memoryProperties_.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            return i;
        }
    }
    for (uint32_t i = 0; i != memoryProperties_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0) return i;
    }
    return UINT32_MAX;
}

bool VulkanNativeHarness::CreateBoundImage(
    VkImage& image, VkDeviceMemory& memory) const noexcept {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.extent = {64, 64, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (createImage_(
            device_, &info, nullptr, &image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    getImageRequirements_(device_, image, &requirements);
    const uint32_t typeIndex =
        SelectMemoryType(requirements.memoryTypeBits);
    if (typeIndex == UINT32_MAX) return false;

    VkMemoryAllocateInfo allocate{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = typeIndex;
    if (allocateMemory_(
            device_, &allocate, nullptr, &memory) != VK_SUCCESS) {
        return false;
    }
    return bindImageMemory_(device_, image, memory, 0) == VK_SUCCESS;
}

void VulkanNativeHarness::DestroyBoundImage(
    VkImage image, VkDeviceMemory memory) const noexcept {
    if (image) destroyImage_(device_, image, nullptr);
    if (memory) freeMemory_(device_, memory, nullptr);
}

bool VulkanNativeHarness::CreatePrimaryCommandBuffer(
    VkCommandPool& pool,
    VkCommandBuffer& command) const noexcept {
    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily_;
    if (createCommandPool_(
            device_, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferAllocateInfo commandInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    return allocateCommands_(
        device_, &commandInfo, &command) == VK_SUCCESS;
}

void VulkanNativeHarness::DestroyCommandPool(
    VkCommandPool pool) const noexcept {
    if (pool) destroyCommandPool_(device_, pool, nullptr);
}

bool VulkanNativeHarness::Begin(
    VkCommandBuffer command) const noexcept {
    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    return beginCommand_(command, &begin) == VK_SUCCESS;
}

bool VulkanNativeHarness::EndSubmitAndWait(
    VkCommandBuffer command) const noexcept {
    if (endCommand_(command) != VK_SUCCESS) return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    return queueSubmit_(
               queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           queueWaitIdle_(queue_) == VK_SUCCESS;
}

} // namespace nrfusion::test
