if (WIN32)
    add_executable(
        nrfusion_d3d12_command_state_restore_tests
        tests/d3d12_command_state_restore_tests.cpp)
    target_link_libraries(
        nrfusion_d3d12_command_state_restore_tests
        PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(
            nrfusion_d3d12_command_state_restore_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(
            nrfusion_d3d12_command_state_restore_tests PRIVATE -UNDEBUG)
    endif()
    add_test(
        NAME nrfusion_d3d12_command_state_restore_tests
        COMMAND nrfusion_d3d12_command_state_restore_tests)
endif()
