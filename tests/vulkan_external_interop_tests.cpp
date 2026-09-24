#include "D3D12ExternalShareHarness.hpp"
#include "VulkanExternalInteropHarness.hpp"

#include "nrfusion/SyntheticVulkanProvider.hpp"

#include <windows.h>

#include <cassert>

using namespace nrfusion;
using namespace nrfusion::test;

namespace {

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

int SkipOrFail(bool required, int code) {
    return required ? code : 0;
}

} // namespace

int main() {
    const bool required = HardwareRequired();

    D3D12ExternalShareHarness d3d12;
    if (!d3d12.Open(required))
        return SkipOrFail(required, 1);

    VulkanExternalInteropHarness vulkan;
    if (!vulkan.Open(d3d12.AdapterLuid(), required))
        return SkipOrFail(required, 2);

    SyntheticVulkanProvider provider;
    if (!provider.Initialize(vulkan.Context()))
        return SkipOrFail(required, 3);

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
        return SkipOrFail(required, 4);
    }

    ImportedVulkanSemaphore producer{};
    ImportedVulkanSemaphore consumer{};
    if (!provider.ImportD3D12Fence(
            d3d12.ProducerFenceHandle(), producer) ||
        !provider.ImportD3D12Fence(
            d3d12.ConsumerFenceHandle(), consumer)) {
        return SkipOrFail(required, 5);
    }

    assert(color.vkImage && color.vkMemory);
    assert(output.vkImage && output.vkMemory);
    assert(producer.vkSemaphore);
    assert(consumer.vkSemaphore);

    assert(HandleStillCallerOwned(d3d12.ColorHandle()));
    assert(HandleStillCallerOwned(d3d12.OutputHandle()));
    assert(HandleStillCallerOwned(d3d12.ProducerFenceHandle()));
    assert(HandleStillCallerOwned(d3d12.ConsumerFenceHandle()));

    assert(d3d12.SignalProducer(1));
    assert(vulkan.SubmitWaitSignal(
        producer.vkSemaphore, 1,
        consumer.vkSemaphore, 1));
    assert(d3d12.WaitConsumer(1, 5000));
    assert(provider.QueryTimelineSemaphore(
        consumer.vkSemaphore) >= 1);

    provider.Shutdown();
    vulkan.Close();
    d3d12.Close();
    return 0;
}
