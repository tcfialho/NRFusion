#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace nrfusion {

// Self-contained Vulkan ABI types for Windows External Memory and Semaphore interop
using VkFlags = uint32_t;
using VkDeviceSize = uint64_t;

enum VkStructureType {
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
    VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO = 1000072000,
    VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR = 1000073000,
    VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO = 1000127000,
    VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR = 1000078000,
    VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO = 1000207002,
    VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO = 1000207004
};

enum VkExternalMemoryHandleTypeFlagBits {
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT = 0x00000080
};

enum VkExternalSemaphoreHandleTypeFlagBits {
    VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT = 0x00000010
};

enum VkSemaphoreType {
    VK_SEMAPHORE_TYPE_BINARY = 0,
    VK_SEMAPHORE_TYPE_TIMELINE = 1
};

struct VkExtent3D {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
};

struct VkExternalMemoryImageCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags handleTypes = 0;
};

struct VkImageCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t imageType = 1; // 2D
    uint32_t format = 0;
    VkExtent3D extent{};
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    uint32_t samples = 1;
    uint32_t tiling = 0;
    VkFlags usage = 0;
    uint32_t sharingMode = 0;
    uint32_t queueFamilyIndexCount = 0;
    const uint32_t* pQueueFamilyIndices = nullptr;
    uint32_t initialLayout = 0;
};

struct VkMemoryRequirements {
    VkDeviceSize size = 0;
    VkDeviceSize alignment = 0;
    uint32_t memoryTypeBits = 0;
};

struct VkMemoryDedicatedAllocateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    const void* pNext = nullptr;
    void* image = nullptr;
    void* buffer = nullptr;
};

struct VkImportMemoryWin32HandleInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    const void* pNext = nullptr;
    VkFlags handleType = 0;
    HANDLE handle = nullptr;
    LPCWSTR name = nullptr;
};

struct VkMemoryAllocateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    const void* pNext = nullptr;
    VkDeviceSize allocationSize = 0;
    uint32_t memoryTypeIndex = 0;
};

struct VkSemaphoreTypeCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    const void* pNext = nullptr;
    VkSemaphoreType semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    uint64_t initialValue = 0;
};

struct VkSemaphoreCreateInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
};

struct VkImportSemaphoreWin32HandleInfoKHR {
    VkStructureType sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    const void* pNext = nullptr;
    void* semaphore = nullptr;
    VkFlags flags = 0;
    VkFlags handleType = 0;
    HANDLE handle = nullptr;
    LPCWSTR name = nullptr;
};

struct VkSemaphoreWaitInfo {
    VkStructureType sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    uint32_t semaphoreCount = 0;
    const void* const* pSemaphores = nullptr;
    const uint64_t* pValues = nullptr;
};

struct VulkanDispatchTable {
    HMODULE libVulkan = nullptr;
    void* (*vkGetInstanceProcAddr)(void* instance, const char* name) = nullptr;
    void* (*vkGetDeviceProcAddr)(void* device, const char* name) = nullptr;

    int (*vkCreateSemaphore)(void* device, const VkSemaphoreCreateInfo* pCreateInfo, const void* pAllocator, void** pSemaphore) = nullptr;
    void (*vkDestroySemaphore)(void* device, void* semaphore, const void* pAllocator) = nullptr;
    int (*vkImportSemaphoreWin32HandleKHR)(void* device, const VkImportSemaphoreWin32HandleInfoKHR* pImportInfo) = nullptr;

    int (*vkCreateImage)(void* device, const VkImageCreateInfo* pCreateInfo, const void* pAllocator, void** pImage) = nullptr;
    void (*vkDestroyImage)(void* device, void* image, const void* pAllocator) = nullptr;
    void (*vkGetImageMemoryRequirements)(void* device, void* image, VkMemoryRequirements* pMemoryRequirements) = nullptr;

    int (*vkAllocateMemory)(void* device, const VkMemoryAllocateInfo* pAllocateInfo, const void* pAllocator, void** pMemory) = nullptr;
    void (*vkFreeMemory)(void* device, void* memory, const void* pAllocator) = nullptr;
    int (*vkBindImageMemory)(void* device, void* image, void* memory, VkDeviceSize memoryOffset) = nullptr;

    int (*vkWaitSemaphores)(void* device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) = nullptr;
    int (*vkGetSemaphoreCounterValue)(void* device, void* semaphore, uint64_t* pValue) = nullptr;

    void (*vkCmdPipelineBarrier)(void* commandBuffer, VkFlags srcStageMask, VkFlags dstStageMask, VkFlags dependencyFlags, uint32_t memoryBarrierCount, const void* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const void* pImageMemoryBarriers) = nullptr;
    void (*vkCmdCopyImage)(void* commandBuffer, void* srcImage, uint32_t srcImageLayout, void* dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions) = nullptr;
    void (*vkCmdBlitImage)(void* commandBuffer, void* srcImage, uint32_t srcImageLayout, void* dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions, uint32_t filter) = nullptr;

    bool isLoaded = false;
};

struct ImportedVulkanResource {
    void* vkImage = nullptr;
    void* vkMemory = nullptr;
    HANDLE d3d12Handle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImportedVulkanSemaphore {
    void* vkSemaphore = nullptr;
    HANDLE d3d12FenceHandle = nullptr;
};

class SyntheticVulkanProvider : public ISyntheticProvider {
public:
    SyntheticVulkanProvider();
    ~SyntheticVulkanProvider() override;

    bool Initialize(const ProviderContext& context) override;
    void Shutdown() override;
    bool IsReady() const noexcept override { return ready_; }

    SyntheticWorkHandle Submit(const SyntheticFrameInputs& inputs, void* commandList) override;
    bool Poll(const SyntheticWorkHandle& handle) override;
    ResourceRef GetResidual(const SyntheticWorkHandle& handle) override;
    bool ComposeNative(const SyntheticWorkHandle& handle,
                       const ResourceRef& originalNative,
                       const ResourceRef& destinationNative,
                       void* commandList,
                       float residualWeight = 1.0f) override;

    const char* Name() const noexcept override { return "SyntheticVulkanProvider"; }

    // Vulkan external memory and semaphore interop methods
    bool LoadVulkanLoader();
    bool IsVulkanLoaderLoaded() const noexcept { return vk_.isLoaded; }

    bool ImportD3D12Resource(HANDLE sharedHandle,
                            uint32_t width,
                            uint32_t height,
                            uint32_t format,
                            uint64_t allocationSize,
                            ImportedVulkanResource& outResource);

    bool ImportD3D12Fence(HANDLE sharedFenceHandle,
                          ImportedVulkanSemaphore& outSemaphore);

    bool WaitTimelineSemaphore(void* vkSemaphore, uint64_t value, uint32_t timeoutMs);
    uint64_t QueryTimelineSemaphore(void* vkSemaphore);

    // Optimized GPU pipeline barrier and transfer/blit fallback for multi-driver compatibility
    bool TransitionImageLayout(void* cmdBuffer, void* image, uint32_t oldLayout, uint32_t newLayout);
    bool BlitOrCopy(void* cmdBuffer, void* srcImage, uint32_t srcWidth, uint32_t srcHeight,
                    void* dstImage, uint32_t dstWidth, uint32_t dstHeight);

private:
    bool ready_ = false;
    void* vkDevice_ = nullptr;
    VulkanDispatchTable vk_{};

    std::unique_ptr<SyntheticDx12Provider> dx12Backend_;
    std::vector<ImportedVulkanResource> importedResources_;
    std::vector<ImportedVulkanSemaphore> importedSemaphores_;
    uint64_t currentWorkId_ = 0;
};

} // namespace nrfusion
