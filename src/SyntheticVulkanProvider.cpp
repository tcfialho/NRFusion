#include "nrfusion/SyntheticVulkanProvider.hpp"\n\nnamespace nrfusion {\n\nSyntheticVulkanProvider::SyntheticVulkanProvider() = default;

SyntheticVulkanProvider::~SyntheticVulkanProvider() {
    Shutdown();
}\n\nbool SyntheticVulkanProvider::Initialize(const ProviderContext& context) {
    LoadVulkanLoader();

    if (context.device && context.api == GraphicsApi::Vulkan && vk_.isLoaded) {
        vkDevice_ = context.device;

        #define RESOLVE_VK(member, name) \
            vk_.member = reinterpret_cast<decltype(vk_.member)>(vk_.vkGetDeviceProcAddr(vkDevice_, name));

        RESOLVE_VK(vkCreateSemaphore, "vkCreateSemaphore");
        RESOLVE_VK(vkDestroySemaphore, "vkDestroySemaphore");
        RESOLVE_VK(vkImportSemaphoreWin32HandleKHR, "vkImportSemaphoreWin32HandleKHR");
        RESOLVE_VK(vkCreateImage, "vkCreateImage");
        RESOLVE_VK(vkDestroyImage, "vkDestroyImage");
        RESOLVE_VK(vkGetImageMemoryRequirements, "vkGetImageMemoryRequirements");
        RESOLVE_VK(vkAllocateMemory, "vkAllocateMemory");
        RESOLVE_VK(vkFreeMemory, "vkFreeMemory");
        RESOLVE_VK(vkBindImageMemory, "vkBindImageMemory");
        RESOLVE_VK(vkWaitSemaphores, "vkWaitSemaphores");
        if (!vk_.vkWaitSemaphores) {
            RESOLVE_VK(vkWaitSemaphores, "vkWaitSemaphoresKHR");
        }
        RESOLVE_VK(vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValue");
        if (!vk_.vkGetSemaphoreCounterValue) {
            RESOLVE_VK(vkGetSemaphoreCounterValue, "vkGetSemaphoreCounterValueKHR");
        }
        RESOLVE_VK(vkCmdPipelineBarrier, "vkCmdPipelineBarrier");
        RESOLVE_VK(vkCmdCopyImage, "vkCmdCopyImage");
        RESOLVE_VK(vkCmdBlitImage, "vkCmdBlitImage");

        #undef RESOLVE_VK
    }

    if (context.device && context.api == GraphicsApi::D3D12) {
        dx12Backend_ = std::make_unique<SyntheticDx12Provider>();
        dx12Backend_->Initialize(context);
    }

    ready_ = true;
    return true;
}

void SyntheticVulkanProvider::Shutdown() {
    if (!ready_) return;

    if (vkDevice_ && vk_.isLoaded) {
        for (auto& sem : importedSemaphores_) {
            if (sem.vkSemaphore && vk_.vkDestroySemaphore) {
                vk_.vkDestroySemaphore(vkDevice_, sem.vkSemaphore, nullptr);
            }
        }
        for (auto& res : importedResources_) {
            if (res.vkImage && vk_.vkDestroyImage) {
                vk_.vkDestroyImage(vkDevice_, res.vkImage, nullptr);
            }
            if (res.vkMemory && vk_.vkFreeMemory) {
                vk_.vkFreeMemory(vkDevice_, res.vkMemory, nullptr);
            }
        }
    }

    importedSemaphores_.clear();
    importedResources_.clear();

    if (dx12Backend_) {
        dx12Backend_->Shutdown();
        dx12Backend_.reset();
    }

    if (vk_.libVulkan) {
        FreeLibrary(vk_.libVulkan);
        vk_.libVulkan = nullptr;
    }
    vk_ = {};
    vkDevice_ = nullptr;
    ready_ = false;
}\n\nSyntheticWorkHandle SyntheticVulkanProvider::Submit(const SyntheticFrameInputs& inputs, void* commandList) {
    currentWorkId_++;
    if (dx12Backend_) {
        return dx12Backend_->Submit(inputs, commandList);
    }
    SyntheticWorkHandle handle{};
    handle.workId = inputs.ticket.id ? inputs.ticket.id : currentWorkId_;
    handle.fenceValue = currentWorkId_;
    handle.valid = true;
    handle.completed = true;
    handle.workingScale = inputs.workingScale;
    handle.workResolution = inputs.renderResolution;
    handle.nativeResolution = inputs.targetResolution;
    return handle;
}

bool SyntheticVulkanProvider::Poll(const SyntheticWorkHandle& handle) {
    if (dx12Backend_) {
        return dx12Backend_->Poll(handle);
    }
    return true;
}

ResourceRef SyntheticVulkanProvider::GetResidual(const SyntheticWorkHandle& handle) {
    if (dx12Backend_) {
        return dx12Backend_->GetResidual(handle);
    }
    ResourceRef ref{};
    ref.opaqueId = 0x0000BAAD;
    ref.resolution = handle.workResolution;
    ref.format = ResourceFormat::Rgba16Float;
    return ref;
}

bool SyntheticVulkanProvider::ComposeNative(const SyntheticWorkHandle& handle,
                                           const ResourceRef& originalNative,
                                           const ResourceRef& destinationNative,
                                           void* commandList,
                                           float residualWeight) {
    if (dx12Backend_) {
        return dx12Backend_->ComposeNative(handle, originalNative, destinationNative, commandList, residualWeight);
    }
    return true;
}\n\n} // namespace nrfusion\n