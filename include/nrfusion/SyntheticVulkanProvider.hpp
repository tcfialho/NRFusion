#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/SyntheticProvider.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace nrfusion {

struct VulkanRawDispatch {
    HMODULE libVulkan = nullptr;
    FARPROC getInstanceProcAddr = nullptr;
    FARPROC getDeviceProcAddr = nullptr;
    FARPROC getPhysicalDeviceImageFormatProperties2 = nullptr;
    FARPROC getPhysicalDeviceExternalSemaphoreProperties = nullptr;
    FARPROC createSemaphore = nullptr;
    FARPROC destroySemaphore = nullptr;
    FARPROC importSemaphoreWin32Handle = nullptr;
    FARPROC createImage = nullptr;
    FARPROC destroyImage = nullptr;
    FARPROC getImageMemoryRequirements = nullptr;
    FARPROC allocateMemory = nullptr;
    FARPROC freeMemory = nullptr;
    FARPROC bindImageMemory = nullptr;
    FARPROC waitSemaphores = nullptr;
    FARPROC getSemaphoreCounterValue = nullptr;
    FARPROC cmdPipelineBarrier = nullptr;
    FARPROC cmdCopyImage = nullptr;
    FARPROC cmdBlitImage = nullptr;
    bool isLoaded = false;
    bool nativeResolved = false;
};

struct ImportedVulkanResource {
    void* vkImage = nullptr;
    void* vkMemory = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImportedVulkanSemaphore {
    void* vkSemaphore = nullptr;
};

class SyntheticVulkanProvider : public ISyntheticProvider {
public:
    SyntheticVulkanProvider();
    ~SyntheticVulkanProvider() override;

    bool Initialize(const ProviderContext& context) override;
    void Shutdown() override;
    bool IsReady() const noexcept override { return ready_; }

    SyntheticWorkHandle Submit(
        const SyntheticFrameInputs& inputs, void* commandList) override;
    bool Poll(const SyntheticWorkHandle& handle) override;
    ResourceRef GetResidual(const SyntheticWorkHandle& handle) override;
    bool ComposeNative(
        const SyntheticWorkHandle& handle,
        const ResourceRef& originalNative,
        const ResourceRef& destinationNative,
        void* commandList,
        float residualWeight = 1.0f) override;

    const char* Name() const noexcept override {
        return "SyntheticVulkanProvider";
    }

    bool LoadVulkanLoader();
    bool IsVulkanLoaderLoaded() const noexcept { return vk_.isLoaded; }

    bool ImportD3D12Resource(
        HANDLE sharedHandle,
        uint32_t width,
        uint32_t height,
        uint32_t format,
        uint64_t allocationSize,
        ImportedVulkanResource& outResource);

    bool ImportD3D12Fence(
        HANDLE sharedFenceHandle,
        ImportedVulkanSemaphore& outSemaphore);

    bool WaitTimelineSemaphore(
        void* vkSemaphore, uint64_t value, uint32_t timeoutMs);
    uint64_t QueryTimelineSemaphore(void* vkSemaphore);

    bool TransitionImageLayout(
        void* cmdBuffer, void* image,
        uint32_t oldLayout, uint32_t newLayout);
    bool AcquireExternalImage(
        void* cmdBuffer, void* image,
        uint32_t externalLayout, uint32_t localLayout);
    bool ReleaseExternalImage(
        void* cmdBuffer, void* image,
        uint32_t localLayout, uint32_t externalLayout);
    bool BlitOrCopy(
        void* cmdBuffer,
        void* srcImage, uint32_t srcWidth, uint32_t srcHeight,
        void* dstImage, uint32_t dstWidth, uint32_t dstHeight);

private:
    bool InitializeNativeVulkan(const ProviderContext& context);
    void ShutdownNativeVulkan() noexcept;

    bool ready_ = false;
    void* vkInstance_ = nullptr;
    void* vkPhysicalDevice_ = nullptr;
    void* vkDevice_ = nullptr;
    void* vkQueue_ = nullptr;
    uint32_t vkQueueFamilyIndex_ = UINT32_MAX;
    VulkanRawDispatch vk_{};

    std::unique_ptr<SyntheticDx12Provider> dx12Backend_;
    std::vector<ImportedVulkanResource> importedResources_;
    std::vector<ImportedVulkanSemaphore> importedSemaphores_;
    uint64_t currentWorkId_ = 0;
};

} // namespace nrfusion
