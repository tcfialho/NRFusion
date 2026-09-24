#include "nrfusion/VulkanNativeCommands.hpp"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace nrfusion;

namespace {

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_VULKAN_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

template <typename T>
T LoadInstanceProc(
    PFN_vkGetInstanceProcAddr getProc,
    VkInstance instance,
    const char* name) {
    return reinterpret_cast<T>(getProc(instance, name));
}

template <typename T>
T LoadDeviceProc(
    PFN_vkGetDeviceProcAddr getProc,
    VkDevice device,
    const char* name) {
    return reinterpret_cast<T>(getProc(device, name));
}

uint32_t SelectMemoryType(
    uint32_t bits,
    const VkPhysicalDeviceMemoryProperties& properties) {
    for (uint32_t i = 0; i != properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            return i;
        }
    }
    for (uint32_t i = 0; i != properties.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0) return i;
    }
    return UINT32_MAX;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) return required ? 1 : 0;

    const auto getInstanceProc =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(loader, "vkGetInstanceProcAddr"));
    assert(getInstanceProc);

    const auto createInstance =
        LoadInstanceProc<PFN_vkCreateInstance>(
            getInstanceProc, VK_NULL_HANDLE, "vkCreateInstance");
    assert(createInstance);

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "NRFusion Vulkan native harness";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    if (createInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        FreeLibrary(loader);
        return required ? 2 : 0;
    }

    const auto destroyInstance =
        LoadInstanceProc<PFN_vkDestroyInstance>(
            getInstanceProc, instance, "vkDestroyInstance");
    const auto enumerateDevices =
        LoadInstanceProc<PFN_vkEnumeratePhysicalDevices>(
            getInstanceProc, instance, "vkEnumeratePhysicalDevices");
    const auto getQueueProperties =
        LoadInstanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            getInstanceProc, instance,
            "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto getDeviceProperties =
        LoadInstanceProc<PFN_vkGetPhysicalDeviceProperties>(
            getInstanceProc, instance, "vkGetPhysicalDeviceProperties");
    const auto getMemoryProperties =
        LoadInstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
            getInstanceProc, instance,
            "vkGetPhysicalDeviceMemoryProperties");
    const auto createDevice =
        LoadInstanceProc<PFN_vkCreateDevice>(
            getInstanceProc, instance, "vkCreateDevice");
    const auto getDeviceProc =
        LoadInstanceProc<PFN_vkGetDeviceProcAddr>(
            getInstanceProc, instance, "vkGetDeviceProcAddr");
    assert(destroyInstance && enumerateDevices && getQueueProperties &&
           getDeviceProperties && getMemoryProperties &&
           createDevice && getDeviceProc);

    uint32_t deviceCount = 0;
    enumerateDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        destroyInstance(instance, nullptr);
        FreeLibrary(loader);
        return required ? 3 : 0;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    enumerateDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        getDeviceProperties(candidate, &properties);
        if (required && properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
            continue;

        uint32_t count = 0;
        getQueueProperties(candidate, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        getQueueProperties(candidate, &count, queues.data());
        for (uint32_t i = 0; i != count; ++i) {
            if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                physical = candidate;
                queueFamily = i;
                break;
            }
        }
        if (physical != VK_NULL_HANDLE) break;
    }
    if (physical == VK_NULL_HANDLE) {
        destroyInstance(instance, nullptr);
        FreeLibrary(loader);
        return required ? 4 : 0;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device = VK_NULL_HANDLE;
    assert(createDevice(physical, &deviceInfo, nullptr, &device) == VK_SUCCESS);

    const auto destroyDevice =
        LoadDeviceProc<PFN_vkDestroyDevice>(
            getDeviceProc, device, "vkDestroyDevice");
    const auto getDeviceQueue =
        LoadDeviceProc<PFN_vkGetDeviceQueue>(
            getDeviceProc, device, "vkGetDeviceQueue");
    const auto createImage =
        LoadDeviceProc<PFN_vkCreateImage>(
            getDeviceProc, device, "vkCreateImage");
    const auto destroyImage =
        LoadDeviceProc<PFN_vkDestroyImage>(
            getDeviceProc, device, "vkDestroyImage");
    const auto getRequirements =
        LoadDeviceProc<PFN_vkGetImageMemoryRequirements>(
            getDeviceProc, device, "vkGetImageMemoryRequirements");
    const auto allocateMemory =
        LoadDeviceProc<PFN_vkAllocateMemory>(
            getDeviceProc, device, "vkAllocateMemory");
    const auto freeMemory =
        LoadDeviceProc<PFN_vkFreeMemory>(
            getDeviceProc, device, "vkFreeMemory");
    const auto bindImageMemory =
        LoadDeviceProc<PFN_vkBindImageMemory>(
            getDeviceProc, device, "vkBindImageMemory");
    const auto createCommandPool =
        LoadDeviceProc<PFN_vkCreateCommandPool>(
            getDeviceProc, device, "vkCreateCommandPool");
    const auto destroyCommandPool =
        LoadDeviceProc<PFN_vkDestroyCommandPool>(
            getDeviceProc, device, "vkDestroyCommandPool");
    const auto allocateCommands =
        LoadDeviceProc<PFN_vkAllocateCommandBuffers>(
            getDeviceProc, device, "vkAllocateCommandBuffers");
    const auto beginCommand =
        LoadDeviceProc<PFN_vkBeginCommandBuffer>(
            getDeviceProc, device, "vkBeginCommandBuffer");
    const auto endCommand =
        LoadDeviceProc<PFN_vkEndCommandBuffer>(
            getDeviceProc, device, "vkEndCommandBuffer");
    const auto queueSubmit =
        LoadDeviceProc<PFN_vkQueueSubmit>(
            getDeviceProc, device, "vkQueueSubmit");
    const auto queueWaitIdle =
        LoadDeviceProc<PFN_vkQueueWaitIdle>(
            getDeviceProc, device, "vkQueueWaitIdle");
    assert(destroyDevice && getDeviceQueue && createImage &&
           destroyImage && getRequirements && allocateMemory &&
           freeMemory && bindImageMemory && createCommandPool &&
           destroyCommandPool && allocateCommands && beginCommand &&
           endCommand && queueSubmit && queueWaitIdle);

    VkQueue queue = VK_NULL_HANDLE;
    getDeviceQueue(device, queueFamily, 0, &queue);
    assert(queue != VK_NULL_HANDLE);

    VulkanContextContract contract{};
    contract.instance = reinterpret_cast<std::uintptr_t>(instance);
    contract.physicalDevice = reinterpret_cast<std::uintptr_t>(physical);
    contract.device = reinterpret_cast<std::uintptr_t>(device);
    contract.queue = reinterpret_cast<std::uintptr_t>(queue);
    contract.queueFamilyIndex = queueFamily;
    assert(ValidateVulkanContextContract(contract));
    assert(MakeVulkanNativeContext(contract).Valid());

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    getMemoryProperties(physical, &memoryProperties);

    auto createBoundImage = [&](VkImage& image, VkDeviceMemory& memory) {
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
        assert(createImage(device, &info, nullptr, &image) == VK_SUCCESS);

        VkMemoryRequirements requirements{};
        getRequirements(device, image, &requirements);
        const uint32_t typeIndex =
            SelectMemoryType(requirements.memoryTypeBits, memoryProperties);
        assert(typeIndex != UINT32_MAX);

        VkMemoryAllocateInfo allocate{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = typeIndex;
        assert(allocateMemory(
            device, &allocate, nullptr, &memory) == VK_SUCCESS);
        assert(bindImageMemory(device, image, memory, 0) == VK_SUCCESS);
    };

    VkImage source = VK_NULL_HANDLE;
    VkImage destination = VK_NULL_HANDLE;
    VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
    VkDeviceMemory destinationMemory = VK_NULL_HANDLE;
    createBoundImage(source, sourceMemory);
    createBoundImage(destination, destinationMemory);

    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    assert(createCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS);

    VkCommandBufferAllocateInfo commandInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = pool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    assert(allocateCommands(device, &commandInfo, &command) == VK_SUCCESS);

    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    assert(beginCommand(command, &begin) == VK_SUCCESS);

    VulkanCommandDispatch dispatch{};
    dispatch.pipelineBarrier =
        LoadDeviceProc<PFN_vkCmdPipelineBarrier>(
            getDeviceProc, device, "vkCmdPipelineBarrier");
    dispatch.copyImage =
        LoadDeviceProc<PFN_vkCmdCopyImage>(
            getDeviceProc, device, "vkCmdCopyImage");
    dispatch.blitImage =
        LoadDeviceProc<PFN_vkCmdBlitImage>(
            getDeviceProc, device, "vkCmdBlitImage");
    assert(dispatch.Valid());

    assert(RecordVulkanImageTransition(
        dispatch, command, source,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL));
    assert(RecordVulkanImageTransition(
        dispatch, command, destination,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL));
    assert(RecordVulkanCopyOrBlit(
        dispatch, command, source, 64, 64,
        destination, 64, 64));
    assert(endCommand(command) == VK_SUCCESS);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    assert(queueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
    assert(queueWaitIdle(queue) == VK_SUCCESS);

    destroyCommandPool(device, pool, nullptr);
    destroyImage(device, destination, nullptr);
    freeMemory(device, destinationMemory, nullptr);
    destroyImage(device, source, nullptr);
    freeMemory(device, sourceMemory, nullptr);
    destroyDevice(device, nullptr);
    destroyInstance(instance, nullptr);
    FreeLibrary(loader);

    std::puts("NRFusion Vulkan native VkDevice harness passed.");
    return 0;
}
