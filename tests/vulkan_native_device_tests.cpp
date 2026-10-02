#include "VulkanNativeHarness.hpp"

#include <windows.h>

#include <cassert>
#include <cstdio>

using namespace nrfusion;
using namespace nrfusion::test;

namespace {

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_VULKAN_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    VulkanNativeHarness harness;
    if (!harness.Open(required)) return required ? 1 : 0;

    const VulkanContextContract context = harness.ContextContract();
    assert(ValidateVulkanContextContract(context));
    assert(MakeVulkanNativeContext(context).Valid());

    VkImage source = VK_NULL_HANDLE;
    VkImage destination = VK_NULL_HANDLE;
    VkDeviceMemory sourceMemory = VK_NULL_HANDLE;
    VkDeviceMemory destinationMemory = VK_NULL_HANDLE;
    assert(harness.CreateBoundImage(source, sourceMemory));
    assert(harness.CreateBoundImage(destination, destinationMemory));

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    assert(harness.CreatePrimaryCommandBuffer(pool, command));
    assert(harness.Begin(command));

    const VulkanCommandDispatch dispatch = harness.CommandDispatch();
    assert(dispatch.Valid());
    assert(RecordVulkanImageTransition(
        dispatch, command, source,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL));
    assert(RecordVulkanImageTransition(
        dispatch, command, destination,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL));
    assert(RecordVulkanCopyOrBlit(
        dispatch, command, source, 64, 64,
        destination, 64, 64));
    assert(harness.EndSubmitAndWait(command));

    harness.DestroyCommandPool(pool);
    harness.DestroyBoundImage(destination, destinationMemory);
    harness.DestroyBoundImage(source, sourceMemory);
    harness.Close();

    std::puts("NRFusion Vulkan native VkDevice harness passed.");
    return 0;
}
