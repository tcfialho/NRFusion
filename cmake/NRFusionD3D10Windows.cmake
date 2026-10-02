add_executable(nrfusion_d3d10_external_bridge_tests
    tests/d3d10_external_bridge_tests.cpp
    tests/D3D10ExternalBridgeHarnessDevices.cpp
    tests/D3D10ExternalBridgeHarnessResources.cpp
    tests/D3D10ExternalBridgeHarnessRun.cpp)
target_include_directories(nrfusion_d3d10_external_bridge_tests PRIVATE
    include tests)
target_link_libraries(nrfusion_d3d10_external_bridge_tests PRIVATE
    nrfusion_core d3d10_1 d3d11 d3d12 dxgi)
if (MSVC)
    target_compile_options(nrfusion_d3d10_external_bridge_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_d3d10_external_bridge_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_d3d10_external_bridge_tests
    COMMAND nrfusion_d3d10_external_bridge_tests)
set_tests_properties(nrfusion_d3d10_external_bridge_tests PROPERTIES
    SKIP_RETURN_CODE 77)
