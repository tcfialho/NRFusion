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
constexpr std::uint32_t kRecreationCycles = 32;
constexpr std::uint32_t kReuseCycles = 128;

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

bool SubmitImportedCycle(
    D3D12ExternalShareHarness& d3d12,
    VulkanExternalInteropHarness& vulkan,
    SyntheticVulkanProvider& provider,
    const ImportedVulkanResource& color,
    const ImportedVulkanResource& output,
    const ImportedVulkanSemaphore& producer,
    const ImportedVulkanSemaphore& consumer,
    std::uint64_t value,
    bool firstUse) {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (!vulkan.CreatePrimaryCommandBuffer(pool, command) ||
        !vulkan.Begin(command)) {
        vulkan.DestroyCommandPool(pool);
        return false;
    }

    const auto general =
        static_cast<std::uint32_t>(VK_IMAGE_LAYOUT_GENERAL);
    const auto incoming = static_cast<std::uint32_t>(
        firstUse ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL);
    const bool recorded =
        provider.AcquireExternalImage(
            command, color.vkImage, incoming, general) &&
        provider.AcquireExternalImage(
            command, output.vkImage, incoming, general) &&
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
    return submitted;
}

bool ImportInteropResources(
    D3D12ExternalShareHarness& d3d12,
    SyntheticVulkanProvider& provider,
    ImportedVulkanResource& color,
    ImportedVulkanResource& output,
    ImportedVulkanSemaphore& producer,
    ImportedVulkanSemaphore& consumer) {
    return provider.ImportD3D12Resource(
               d3d12.ColorHandle(), 64, 64,
               VK_FORMAT_R16G16B16A16_SFLOAT,
               d3d12.AllocationSize(), color) &&
           provider.ImportD3D12Resource(
               d3d12.OutputHandle(), 64, 64,
               VK_FORMAT_R16G16B16A16_SFLOAT,
               d3d12.AllocationSize(), output) &&
           provider.ImportD3D12Fence(
               d3d12.ProducerFenceHandle(), producer) &&
           provider.ImportD3D12Fence(
               d3d12.ConsumerFenceHandle(), consumer);
}

bool RunRecreationCycle(
    D3D12ExternalShareHarness& d3d12,
    VulkanExternalInteropHarness& vulkan,
    std::uint64_t value) {
    SyntheticVulkanProvider provider;
    if (!provider.Initialize(vulkan.Context())) return false;

    ImportedVulkanResource color{};
    ImportedVulkanResource output{};
    ImportedVulkanSemaphore producer{};
    ImportedVulkanSemaphore consumer{};
    const bool imported = ImportInteropResources(
        d3d12, provider, color, output, producer, consumer);
    const bool submitted =
        imported && SubmitImportedCycle(
            d3d12, vulkan, provider,
            color, output, producer, consumer, value, true);
    provider.Shutdown();
    return submitted;
}

bool RunReuseCycles(
    D3D12ExternalShareHarness& d3d12,
    VulkanExternalInteropHarness& vulkan,
    std::uint64_t firstValue) {
    SyntheticVulkanProvider provider;
    if (!provider.Initialize(vulkan.Context())) return false;

    ImportedVulkanResource color{};
    ImportedVulkanResource output{};
    ImportedVulkanSemaphore producer{};
    ImportedVulkanSemaphore consumer{};
    if (!ImportInteropResources(
            d3d12, provider, color, output, producer, consumer)) {
        provider.Shutdown();
        return false;
    }

    bool ok = true;
    for (std::uint32_t cycle = 0; cycle != kReuseCycles && ok; ++cycle) {
        ok = SubmitImportedCycle(
            d3d12, vulkan, provider,
            color, output, producer, consumer,
            firstValue + cycle, cycle == 0);
    }
    provider.Shutdown();
    return ok;
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

    for (std::uint32_t cycle = 0;
         cycle != kRecreationCycles; ++cycle) {
        if (!RunRecreationCycle(d3d12, vulkan, cycle + 1))
            return Unsupported(required, 3);
        assert(HandleStillCallerOwned(d3d12.ColorHandle()));
        assert(HandleStillCallerOwned(d3d12.OutputHandle()));
        assert(HandleStillCallerOwned(d3d12.ProducerFenceHandle()));
        assert(HandleStillCallerOwned(d3d12.ConsumerFenceHandle()));
    }

    if (!RunReuseCycles(d3d12, vulkan, 1000))
        return Unsupported(required, 4);

    vulkan.Close();
    d3d12.Close();
    return 0;
}
