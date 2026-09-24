#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

namespace nrfusion {

struct VulkanImportedMemoryTypeFacts {
    std::uint32_t memoryTypeBits = 0;
    std::uint32_t memoryTypeIndex = 0;
};

bool QueryD3D12ImportedMemoryTypeOpaque(
    std::uintptr_t instance,
    std::uintptr_t physicalDevice,
    std::uintptr_t device,
    std::uintptr_t queue,
    std::uint32_t queueFamilyIndex,
    FARPROC getDeviceProcAddr,
    HANDLE sharedHandle,
    std::uint32_t imageMemoryTypeBits,
    VulkanImportedMemoryTypeFacts& outFacts) noexcept;

} // namespace nrfusion
