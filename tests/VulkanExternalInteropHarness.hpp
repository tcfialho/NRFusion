#pragma once

#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/VulkanNativeContext.hpp"

#include <windows.h>

#include <cstdint>

namespace nrfusion::test {

class VulkanExternalInteropHarness {
public:
    VulkanExternalInteropHarness() = default;
    ~VulkanExternalInteropHarness();

    VulkanExternalInteropHarness(
        const VulkanExternalInteropHarness&) = delete;
    VulkanExternalInteropHarness& operator=(
        const VulkanExternalInteropHarness&) = delete;

    bool Open(const LUID& adapterLuid, bool hardwareRequired);
    void Close() noexcept;

    ProviderContext Context() const noexcept;

    bool SubmitWaitSignal(
        void* waitSemaphore,
        std::uint64_t waitValue,
        void* signalSemaphore,
        std::uint64_t signalValue) noexcept;

private:
    template <typename T>
    T InstanceProc(const char* name) const noexcept {
        return reinterpret_cast<T>(
            getInstanceProc_(instance_, name));
    }

    template <typename T>
    T DeviceProc(const char* name) const noexcept {
        return reinterpret_cast<T>(
            getDeviceProc_(device_, name));
    }

    HMODULE loader_ = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProc_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProc_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = UINT32_MAX;

    PFN_vkDestroyInstance destroyInstance_ = nullptr;
    PFN_vkDestroyDevice destroyDevice_ = nullptr;
    PFN_vkQueueSubmit queueSubmit_ = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle_ = nullptr;
};

} // namespace nrfusion::test
