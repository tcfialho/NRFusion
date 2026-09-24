#include "D3D12ExternalShareHarness.hpp"
#include "VulkanExternalInteropHarness.hpp"

#include "nrfusion/SyntheticVulkanProvider.hpp"

#include <windows.h>

#include <cassert>
#include <cstdint>

using namespace nrfusion;
using namespace nrfusion::test;

namespace {

constexpr int kSkip = 77;
constexpr std::uint32_t kCycles = 32;

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_VULKAN_EXTERNAL_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

bool HandleStillCallerOwned(HANDLE handle) {
    HANDLE duplicate = nullptr;
    const BOOL ok = DuplicateHandle(
        GetCurrentProcess(), handle,
        GetCurrentProcess(), &duplicate,
        0, FALSE, DUPLICATE_SAME_ACCESS);
    if (duplicate) CloseHandle(duplicate);
    return ok == TRUE;
}

int Unsupported(bool required, int code) {
    return required ? code : kSkip;
}

bool RunInteropCycle(
    D3D12ExternalShareHarness& d3d12,
    VulkanExternalInteropHarness& vulkan,
    std::uint64_t value) {
    SyntheticVulkanProvider provider;
    if (!provider.Initialize(vulkan.Context())) return false;

    ImportedVulkanResource color{};
    ImportedVulkanResource output{};
    if (!provider.ImportD3D12Resource(
            d3d12.ColorHandle(), 64, 64,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            d3d12.AllocationSize(), color) ||
        !provider.ImportD3D12Resource(
            d3d12.OutputHandle(), 64, 64,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            d3d12.AllocationSize(), output)) {
        provider.Shutdown();
        return false;
    }

    ImportedVulkanSemaphore producer{};
    ImportedVulkanSemaphore consumer{};
    if (!provider.ImportD3D12Fence(
            d3d12.ProducerFenceHandle(), producer) ||
        !provider.ImportD3D12Fence(
            d3d12.ConsumerFenceHandle(), consumer)) {
        provider.Shutdown();
        return false;
    }

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (!vulkan.CreatePrimaryCommandBuffer(pool, command) ||
        !vulkan.Begin(command)) {
        vulkan.DestroyCommandPool(pool);
        provider.Shutdown();
        return false;
    }

    const auto general =
        static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_GENERAL);
    const auto undefined =
        static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_UNDEFINED);
    const bool recorded =
        provider.AcquireExternalImage(
            command, color.vkImage, undefined, general) &&
        provider.AcquireExternalImage(
            command, output.vkImage, undefined, general) &&
        provider.BlitOrCopy(
            command,
            color.vkImage, color.width, color.height,
            output.vkImage, output.width, output.height) &&
        provider.ReleaseExternalImage(
            command, color.vkImage, general, general) &&
        provider.ReleaseExternalImage(
            command, output.vkImage, general, general);

    const bool submitted =
        recorded &&
        d3d12.SignalProducer(value) &&
        vulkan.EndSubmitWaitSignal(
            command,
            producer.vkSemaphore, value,
            consumer.vkSemaphore, value) &&
        d3d12.WaitConsumer(value, 5000) &&
        provider.QueryTimelineSemaphore(
            consumer.vkSemaphore) >= value;

    vulkan.DestroyCommandPool(pool);
    provider.Shutdown();
    return submitted;
}

} // namespace

int main() {
    const bool required = HardwareRequired();

    D3D12ExternalShareHarness d3d12;
    if (!d3d12.Open(required))
        return Unsupported(required, 1);

    VulkanExternalInteropHarness vulkan;
    if (!vulkan.Open(d3d12.AdapterLuid(), required))
        return Unsupported(required, 2);

    assert(HandleStillCallerOwned(d3d12.ColorHandle()));
    assert(HandleStillCallerOwned(d3d12.OutputHandle()));
    assert(HandleStillCallerOwned(d3d12.ProducerFenceHandle()));
    assert(HandleStillCallerOwned(d3d12.ConsumerFenceHandle()));

    for (std::uint32_t cycle = 0; cycle != kCycles; ++cycle) {
        if (!RunInteropCycle(d3d12, vulkan, cycle + 1))
            return Unsupported(required, 3);
        assert(HandleStillCallerOwned(d3d12.ColorHandle()));
        assert(HandleStillCallerOwned(d3d12.OutputHandle()));
        assert(HandleStillCallerOwned(d3d12.ProducerFenceHandle()));
        assert(HandleStillCallerOwned(d3d12.ConsumerFenceHandle()));
    }

    vulkan.Close();
    d3d12.Close();
    return 0;
}
