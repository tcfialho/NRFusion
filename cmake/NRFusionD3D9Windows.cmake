add_executable(nrfusion_d3d9ex_share_tests
    tests/d3d9ex_share_tests.cpp
    tests/D3D9ExShareHarnessDevices.cpp
    tests/D3D9ExShareHarnessShare.cpp
    tests/D3D9ExShareHarnessReset.cpp)
target_include_directories(nrfusion_d3d9ex_share_tests PRIVATE
    include tests)
target_link_libraries(nrfusion_d3d9ex_share_tests PRIVATE
    nrfusion_core d3d9 d3d11 d3d12 dxgi)
if (MSVC)
    target_compile_options(nrfusion_d3d9ex_share_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_d3d9ex_share_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_d3d9ex_share_tests
    COMMAND nrfusion_d3d9ex_share_tests)
set_tests_properties(nrfusion_d3d9ex_share_tests PROPERTIES
    SKIP_RETURN_CODE 77)
