#include "nrfusion/SyntheticVulkanProvider.hpp"

namespace nrfusion {

SyntheticVulkanProvider::SyntheticVulkanProvider() = default;

SyntheticVulkanProvider::~SyntheticVulkanProvider() {
    Shutdown();
}

bool SyntheticVulkanProvider::LoadVulkanLoader() {
    if (vk_.isLoaded) return true;

    vk_.libVulkan = LoadLibraryW(L"vulkan-1.dll");
    if (!vk_.libVulkan) return false;

    vk_.getInstanceProcAddr =
        GetProcAddress(vk_.libVulkan, "vkGetInstanceProcAddr");
    vk_.getDeviceProcAddr =
        GetProcAddress(vk_.libVulkan, "vkGetDeviceProcAddr");
    if (!vk_.getInstanceProcAddr || !vk_.getDeviceProcAddr) {
        FreeLibrary(vk_.libVulkan);
        vk_ = {};
        return false;
    }

    vk_.isLoaded = true;
    return true;
}

bool SyntheticVulkanProvider::Initialize(const ProviderContext& context) {
    LoadVulkanLoader();

    bool vulkanReady = false;
#if defined(NRFUSION_ENABLE_VULKAN_NATIVE)
    if (context.api == GraphicsApi::Vulkan && vk_.isLoaded) {
        vulkanReady = InitializeNativeVulkan(context);
    }
#endif

    if (context.device && context.api == GraphicsApi::D3D12) {
        dx12Backend_ = std::make_unique<SyntheticDx12Provider>();
        dx12Backend_->Initialize(context);
    }

    ready_ = vulkanReady || (dx12Backend_ && dx12Backend_->IsReady());
    return ready_;
}

void SyntheticVulkanProvider::Shutdown() {
#if defined(NRFUSION_ENABLE_VULKAN_NATIVE)
    ShutdownNativeVulkan();
#else
    importedSemaphores_.clear();
    importedResources_.clear();
    vkInstance_ = nullptr;
    vkPhysicalDevice_ = nullptr;
    vkDevice_ = nullptr;
    vkQueue_ = nullptr;
    vkQueueFamilyIndex_ = UINT32_MAX;
#endif

    if (dx12Backend_) {
        dx12Backend_->Shutdown();
        dx12Backend_.reset();
    }

    if (vk_.libVulkan) FreeLibrary(vk_.libVulkan);
    vk_ = {};
    ready_ = false;
}

SyntheticWorkHandle SyntheticVulkanProvider::Submit(
    const SyntheticFrameInputs& inputs, void* commandList) {
    ++currentWorkId_;
    return dx12Backend_
        ? dx12Backend_->Submit(inputs, commandList)
        : SyntheticWorkHandle{};
}

bool SyntheticVulkanProvider::Poll(const SyntheticWorkHandle& handle) {
    return dx12Backend_ ? dx12Backend_->Poll(handle) : false;
}

ResourceRef SyntheticVulkanProvider::GetResidual(
    const SyntheticWorkHandle& handle) {
    return dx12Backend_
        ? dx12Backend_->GetResidual(handle)
        : ResourceRef{};
}

bool SyntheticVulkanProvider::ComposeNative(
    const SyntheticWorkHandle& handle,
    const ResourceRef& originalNative,
    const ResourceRef& destinationNative,
    void* commandList,
    float residualWeight) {
    return dx12Backend_ &&
        dx12Backend_->ComposeNative(
            handle, originalNative, destinationNative,
            commandList, residualWeight);
}

} // namespace nrfusion
