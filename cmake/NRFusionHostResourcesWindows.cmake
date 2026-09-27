if (WIN32)
    add_executable(
        nrfusion_host64_stop_cleanup_tests
        tests/host64_stop_cleanup_tests.cpp)
    target_include_directories(
        nrfusion_host64_stop_cleanup_tests PRIVATE tests)
    target_link_libraries(
        nrfusion_host64_stop_cleanup_tests
        PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(
            nrfusion_host64_stop_cleanup_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(
            nrfusion_host64_stop_cleanup_tests PRIVATE -UNDEBUG)
    endif()
    add_test(
        NAME nrfusion_host64_stop_cleanup_tests
        COMMAND nrfusion_host64_stop_cleanup_tests)
endif()
