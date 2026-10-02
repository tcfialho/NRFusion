# NRFusion first-party NVNGX bridge target (replaces legacy OptiScaler forwarder).
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_library(nrfusion_nvngx_bridge SHARED
        src/NvngxBridgeParams.cpp
        src/NvngxBridgeSnippet.cpp
        src/NvngxBridgeD3D12.cpp
    )
    target_include_directories(nrfusion_nvngx_bridge PRIVATE include)
    target_link_libraries(nrfusion_nvngx_bridge PRIVATE d3d12)
    target_compile_definitions(nrfusion_nvngx_bridge PRIVATE NRFUSION_NVNGX_BRIDGE_EXPORTS)
    if (MSVC)
        target_compile_options(nrfusion_nvngx_bridge PRIVATE /W4 /permissive-)
    else()
        target_compile_options(nrfusion_nvngx_bridge PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    set_target_properties(nrfusion_nvngx_bridge PROPERTIES OUTPUT_NAME "nvngx.dll_dlssnr")
    add_dependencies(nrfusion_host64 nrfusion_nvngx_bridge)

    add_executable(nrfusion_nvngx_bridge_tests tests/nvngx_bridge_tests.cpp)
    target_link_libraries(nrfusion_nvngx_bridge_tests PRIVATE nrfusion_nvngx_bridge)
    if (MSVC)
        target_compile_options(nrfusion_nvngx_bridge_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_nvngx_bridge_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_nvngx_bridge_tests COMMAND nrfusion_nvngx_bridge_tests)
endif()
