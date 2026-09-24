#include "nrfusion/SyntheticVulkanProvider.hpp"

namespace nrfusion {

SyntheticVulkanProvider::SyntheticVulkanProvider() = default;

SyntheticVulkanProvider::~SyntheticVulkanProvider() {
    Shutdown();
}

bool SyntheticVulkanProvider::LoadVulkanLoader() {
    if (vk_.isLoaded) return true;

    vk_.libVulkan = LoadLibraryW(L"vulkan-1.dll");
    if (!vk_.libVulkan) {
        return false;
    }

    // Going straight from what the loader returns to the signature wanted is a cast between two
    // unrelated function types, which some compilers refuse. Through a plain function pointer the
    // conversion is the one the platform already promises.
    const auto entry = [this](const char* name) {
        return reinterpret_cast<void* (*)(void*, const char*)>(
            reinterpret_cast<void (*)()>(GetProcAddress(vk_.libVulkan, name)));
    };
    vk_.vkGetInstanceProcAddr = entry("vkGetInstanceProcAddr");
    vk_.vkGetDeviceProcAddr = entry("vkGetDeviceProcAddr");

    if (!vk_.vkGetInstanceProcAddr || !vk_.vkGetDeviceProcAddr) {
        FreeLibrary(vk_.libVulkan);
        vk_.libVulkan = nullptr;
        return false;
    }

    vk_.isLoaded = true;
    return true;
}

bool SyntheticVulkanProvider::Initialize(const ProviderContext& context) {
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
}

SyntheticWorkHandle SyntheticVulkanProvider::Submit(const SyntheticFrameInputs& inputs, void* commandList) {
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
}

} // namespace nrfusion
