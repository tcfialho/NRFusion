enable_testing()

function(nrfusion_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE nrfusion_core)
    if (MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

nrfusion_test(nrfusion_tests tests/controller_tests.cpp)
nrfusion_test(nrfusion_game_probe_tests tests/game_probe_tests.cpp)
nrfusion_test(nrfusion_telemetry_tests tests/telemetry_tracker_tests.cpp)
nrfusion_test(nrfusion_timing_mapper_tests tests/timing_mapper_tests.cpp)
nrfusion_test(nrfusion_runtime_shell_tests tests/runtime_shell_tests.cpp)
nrfusion_test(nrfusion_nr_session_tests tests/nr_session_tests.cpp)
nrfusion_test(nrfusion_ngx_feature_registry_tests tests/ngx_feature_registry_tests.cpp)
nrfusion_test(nrfusion_submission_gate_tests tests/submission_gate_tests.cpp)
nrfusion_test(nrfusion_nr_retirement_queue_tests tests/nr_retirement_queue_tests.cpp)

add_executable(nrfusion_d3d12_nr_frame_plan_tests
    tests/d3d12_nr_frame_plan_tests.cpp
    src/D3D12NrFramePlan.cpp
)
target_include_directories(nrfusion_d3d12_nr_frame_plan_tests PRIVATE include)
if (MSVC)
    target_compile_options(nrfusion_d3d12_nr_frame_plan_tests PRIVATE /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_d3d12_nr_frame_plan_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_d3d12_nr_frame_plan_tests COMMAND nrfusion_d3d12_nr_frame_plan_tests)
