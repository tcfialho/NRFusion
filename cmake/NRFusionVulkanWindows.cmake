add_library(nrfusion_vulkan_provider_compile OBJECT
    src/SyntheticVulkanProvider.cpp
    src/SyntheticVulkanProviderInterop.cpp)
target_include_directories(nrfusion_vulkan_provider_compile PRIVATE include)
if (MSVC)
    target_compile_options(nrfusion_vulkan_provider_compile PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(nrfusion_vulkan_provider_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
