#include "nrfusion/SyntheticVulkanProvider.hpp"

namespace nrfusion {

bool SyntheticVulkanProvider::ImportD3D12Resource(HANDLE sharedHandle,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t format,
                                                 uint64_t allocationSize,
                                                 ImportedVulkanResource& outResource) {
    if (!sharedHandle) return false;

    outResource.d3d12Handle = sharedHandle;
    outResource.width = width;
    outResource.height = height;

    if (vkDevice_ && vk_.vkCreateImage && vk_.vkAllocateMemory && vk_.vkBindImageMemory) {
        VkExternalMemoryImageCreateInfo ext{};
        ext.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        ext.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;

        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.pNext = &ext;
        ici.imageType = 1; // 2D
        ici.format = format;
        ici.extent = { width, height, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = 1;
        ici.tiling = 0; // OPTIMAL
        ici.usage = 0x00000001 | 0x00000002 | 0x00000004; // SRC | DST | SAMPLED
        ici.sharingMode = 0; // EXCLUSIVE

        void* image = nullptr;
        if (vk_.vkCreateImage(vkDevice_, &ici, nullptr, &image) != 0) {
            return false;
        }

        VkMemoryRequirements req{};
        vk_.vkGetImageMemoryRequirements(vkDevice_, image, &req);

        VkMemoryDedicatedAllocateInfo ded{};
        ded.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        ded.image = image;

        VkImportMemoryWin32HandleInfoKHR imp{};
        imp.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        imp.pNext = &ded;
        imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        imp.handle = sharedHandle;

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext = &imp;
        mai.allocationSize = (allocationSize != 0) ? allocationSize : req.size;
        mai.memoryTypeIndex = 0;

        void* mem = nullptr;
        if (vk_.vkAllocateMemory(vkDevice_, &mai, nullptr, &mem) != 0) {
            vk_.vkDestroyImage(vkDevice_, image, nullptr);
            return false;
        }

        if (vk_.vkBindImageMemory(vkDevice_, image, mem, 0) != 0) {
            vk_.vkFreeMemory(vkDevice_, mem, nullptr);
            vk_.vkDestroyImage(vkDevice_, image, nullptr);
            return false;
        }

        outResource.vkImage = image;
        outResource.vkMemory = mem;
    }

    importedResources_.push_back(outResource);
    return true;
}

bool SyntheticVulkanProvider::ImportD3D12Fence(HANDLE sharedFenceHandle,
                                              ImportedVulkanSemaphore& outSemaphore) {
    if (!sharedFenceHandle) return false;

    outSemaphore.d3d12FenceHandle = sharedFenceHandle;

    if (vkDevice_ && vk_.vkCreateSemaphore && vk_.vkImportSemaphoreWin32HandleKHR) {
        VkSemaphoreTypeCreateInfo tci{};
        tci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        tci.initialValue = 0;

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sci.pNext = &tci;

        void* sem = nullptr;
        if (vk_.vkCreateSemaphore(vkDevice_, &sci, nullptr, &sem) != 0) {
            return false;
        }

        VkImportSemaphoreWin32HandleInfoKHR imp{};
        imp.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
        imp.semaphore = sem;
        imp.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        imp.handle = sharedFenceHandle;

        if (vk_.vkImportSemaphoreWin32HandleKHR(vkDevice_, &imp) != 0) {
            vk_.vkDestroySemaphore(vkDevice_, sem, nullptr);
            return false;
        }

        outSemaphore.vkSemaphore = sem;
    }

    importedSemaphores_.push_back(outSemaphore);
    return true;
}

bool SyntheticVulkanProvider::WaitTimelineSemaphore(void* vkSemaphore, uint64_t value, uint32_t timeoutMs) {
    if (!vkDevice_ || !vk_.vkWaitSemaphores || !vkSemaphore) {
        return false;
    }

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &vkSemaphore;
    waitInfo.pValues = &value;

    uint64_t timeoutNs = static_cast<uint64_t>(timeoutMs) * 1000000ULL;
    return (vk_.vkWaitSemaphores(vkDevice_, &waitInfo, timeoutNs) == 0);
}

uint64_t SyntheticVulkanProvider::QueryTimelineSemaphore(void* vkSemaphore) {
    if (!vkDevice_ || !vk_.vkGetSemaphoreCounterValue || !vkSemaphore) {
        return 0;
    }

    uint64_t val = 0;
    if (vk_.vkGetSemaphoreCounterValue(vkDevice_, vkSemaphore, &val) == 0) {
        return val;
    }
    return 0;
}

bool SyntheticVulkanProvider::TransitionImageLayout(void* cmdBuffer, void* image, uint32_t oldLayout, uint32_t newLayout) {
    if (!cmdBuffer || !image) return false;
    if (vkDevice_ && vk_.vkCmdPipelineBarrier) {
        struct LocalVkImageMemoryBarrier {
            uint32_t sType = 45; // VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER
            const void* pNext = nullptr;
            uint32_t srcAccessMask = 0;
            uint32_t dstAccessMask = 0;
            uint32_t oldLayout = 0;
            uint32_t newLayout = 0;
            uint32_t srcQueueFamilyIndex = ~0u;
            uint32_t dstQueueFamilyIndex = ~0u;
            void* image = nullptr;
            struct {
                uint32_t aspectMask = 1;
                uint32_t baseMipLevel = 0;
                uint32_t levelCount = 1;
                uint32_t baseArrayLayer = 0;
                uint32_t layerCount = 1;
            } subresourceRange;
        } barrier{};

        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.image = image;

        // Stage masks: ALL_COMMANDS (0x00010000)
        uint32_t stageMask = 0x00010000;
        vk_.vkCmdPipelineBarrier(cmdBuffer, stageMask, stageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    return true;
}

bool SyntheticVulkanProvider::BlitOrCopy(void* cmdBuffer, void* srcImage, uint32_t srcWidth, uint32_t srcHeight,
                                        void* dstImage, uint32_t dstWidth, uint32_t dstHeight) {
    if (!cmdBuffer || !srcImage || !dstImage) return false;

    if (vkDevice_) {
        if (srcWidth == dstWidth && srcHeight == dstHeight && vk_.vkCmdCopyImage) {
            struct LocalVkImageCopy {
                struct { uint32_t aspectMask = 1; uint32_t mipLevel = 0; uint32_t baseArrayLayer = 0; uint32_t layerCount = 1; } srcSubresource;
                struct { int32_t x = 0; int32_t y = 0; int32_t z = 0; } srcOffset;
                struct { uint32_t aspectMask = 1; uint32_t mipLevel = 0; uint32_t baseArrayLayer = 0; uint32_t layerCount = 1; } dstSubresource;
                struct { int32_t x = 0; int32_t y = 0; int32_t z = 0; } dstOffset;
                struct { uint32_t width = 0; uint32_t height = 0; uint32_t depth = 1; } extent;
            } region{};
            region.extent.width = srcWidth;
            region.extent.height = srcHeight;

            // VK_IMAGE_LAYOUT_GENERAL = 1
            vk_.vkCmdCopyImage(cmdBuffer, srcImage, 1, dstImage, 1, 1, &region);
            return true;
        } else if (vk_.vkCmdBlitImage) {
            struct LocalVkImageBlit {
                struct { uint32_t aspectMask = 1; uint32_t mipLevel = 0; uint32_t baseArrayLayer = 0; uint32_t layerCount = 1; } srcSubresource;
                struct { int32_t x; int32_t y; int32_t z; } srcOffsets[2];
                struct { uint32_t aspectMask = 1; uint32_t mipLevel = 0; uint32_t baseArrayLayer = 0; uint32_t layerCount = 1; } dstSubresource;
                struct { int32_t x; int32_t y; int32_t z; } dstOffsets[2];
            } blitRegion{};
            blitRegion.srcOffsets[0] = { 0, 0, 0 };
            blitRegion.srcOffsets[1] = { static_cast<int32_t>(srcWidth), static_cast<int32_t>(srcHeight), 1 };
            blitRegion.dstOffsets[0] = { 0, 0, 0 };
            blitRegion.dstOffsets[1] = { static_cast<int32_t>(dstWidth), static_cast<int32_t>(dstHeight), 1 };

            // VK_FILTER_LINEAR = 1
            vk_.vkCmdBlitImage(cmdBuffer, srcImage, 1, dstImage, 1, 1, &blitRegion, 1);
            return true;
        }
    }
    return true;
}

} // namespace nrfusion
