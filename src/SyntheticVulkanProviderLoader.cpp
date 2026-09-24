#include "nrfusion/SyntheticVulkanProvider.hpp"\n\nnamespace nrfusion {\n\nbool SyntheticVulkanProvider::LoadVulkanLoader() {
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
}\n\n} // namespace nrfusion\n