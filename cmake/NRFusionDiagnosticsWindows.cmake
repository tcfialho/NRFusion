if (WIN32)
    add_executable(nrfusion_nr_kernel_identity_tests
        tests/nr_kernel_identity_tests.cpp src/NrKernelProfileStatistics.cpp)
    target_include_directories(nrfusion_nr_kernel_identity_tests PRIVATE src include)
    if (MSVC)
        target_compile_options(nrfusion_nr_kernel_identity_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_nr_kernel_identity_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_nr_kernel_identity_tests COMMAND nrfusion_nr_kernel_identity_tests)
    add_executable(
        nrfusion_d3d12_diagnostics_resources_tests
        tests/d3d12_diagnostics_resources_tests.cpp
        src/host/NrD3D12Diagnostics.cpp)
    target_include_directories(
        nrfusion_d3d12_diagnostics_resources_tests
        PRIVATE tests/nvapi_stub tests include)
    target_link_libraries(
        nrfusion_d3d12_diagnostics_resources_tests PRIVATE d3d12 dxgi)
    if (MSVC)
        target_compile_options(
            nrfusion_d3d12_diagnostics_resources_tests
            PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(
            nrfusion_d3d12_diagnostics_resources_tests PRIVATE -UNDEBUG)
    endif()
    add_test(
        NAME nrfusion_d3d12_diagnostics_resources_tests
        COMMAND nrfusion_d3d12_diagnostics_resources_tests)
endif()
