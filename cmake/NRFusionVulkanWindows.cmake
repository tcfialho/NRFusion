find_path(NRFUSION_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS
        "$ENV{NRFUSION_VULKAN_HEADERS}"
        "$ENV{VULKAN_SDK}/Include"
        "$ENV{VULKAN_SDK}/include"
)
if (NOT NRFUSION_VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR
        "Official Vulkan headers are required for Phase 11 native validation.")
endif()

add_library(nrfusion_vulkan_provider_compile OBJECT
    src/SyntheticVulkanProvider.cpp
    src/SyntheticVulkanProviderNative.cpp
    src/SyntheticVulkanProviderInterop.cpp
    src/SyntheticVulkanProviderCommands.cpp
    src/VulkanNativeCommands.cpp
    src/VulkanNativeContext.cpp)
target_include_directories(nrfusion_vulkan_provider_compile PRIVATE
    include
    "${NRFUSION_VULKAN_INCLUDE_DIR}")
target_compile_definitions(nrfusion_vulkan_provider_compile PRIVATE
    NRFUSION_ENABLE_VULKAN_NATIVE=1)
if (MSVC)
    target_compile_options(nrfusion_vulkan_provider_compile PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(nrfusion_vulkan_provider_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(nrfusion_vulkan_native_compile OBJECT
    src/VulkanNativeContext.cpp)
target_include_directories(nrfusion_vulkan_native_compile PRIVATE
    include
    "${NRFUSION_VULKAN_INCLUDE_DIR}")
if (MSVC)
    target_compile_options(nrfusion_vulkan_native_compile PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(nrfusion_vulkan_native_compile PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(nrfusion_vulkan_native_device_tests
    tests/vulkan_native_device_tests.cpp
    tests/VulkanNativeHarness.cpp
    src/VulkanNativeCommands.cpp
    src/VulkanNativeContext.cpp
    src/VulkanCarrierContract.cpp)
target_include_directories(nrfusion_vulkan_native_device_tests PRIVATE
    include
    "${NRFUSION_VULKAN_INCLUDE_DIR}")
if (MSVC)
    target_compile_options(nrfusion_vulkan_native_device_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_vulkan_native_device_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_vulkan_native_device_tests
    COMMAND nrfusion_vulkan_native_device_tests)
