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
    src/SyntheticVulkanProviderMemory.cpp
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

add_executable(nrfusion_d3d12_external_share_tests
    tests/d3d12_external_share_tests.cpp
    tests/D3D12ExternalShareHarness.cpp)
target_include_directories(nrfusion_d3d12_external_share_tests PRIVATE
    tests)
target_link_libraries(nrfusion_d3d12_external_share_tests PRIVATE
    d3d12 dxgi)
if (MSVC)
    target_compile_options(nrfusion_d3d12_external_share_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_d3d12_external_share_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_d3d12_external_share_tests
    COMMAND nrfusion_d3d12_external_share_tests)

add_executable(nrfusion_vulkan_external_interop_tests
    tests/vulkan_external_interop_tests.cpp
    tests/VulkanExternalInteropHarness.cpp
    tests/D3D12ExternalShareHarness.cpp
    src/SyntheticDx12Provider.cpp
    src/VulkanCarrierContract.cpp
    $<TARGET_OBJECTS:nrfusion_vulkan_provider_compile>)
target_include_directories(nrfusion_vulkan_external_interop_tests PRIVATE
    include
    tests
    "${NRFUSION_VULKAN_INCLUDE_DIR}")
target_link_libraries(nrfusion_vulkan_external_interop_tests PRIVATE
    d3d12 dxgi d3dcompiler)
if (MSVC)
    target_compile_options(nrfusion_vulkan_external_interop_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_vulkan_external_interop_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_vulkan_external_interop_tests
    COMMAND nrfusion_vulkan_external_interop_tests)
