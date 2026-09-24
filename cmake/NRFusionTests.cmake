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

option(NRFUSION_FOCUSED_NR_SESSION_VALIDATION
    "Build NrSession tests from a portable production-source slice" OFF)

if (NRFUSION_FOCUSED_NR_SESSION_VALIDATION)
    set(NRFUSION_NR_SESSION_PORTABLE_SOURCES
        src/PerformanceController.cpp
        src/PerformanceControllerLifecycle.cpp
        src/PerformanceControllerScale.cpp
        src/NrCostModel.cpp
        src/PrecisionAutotuner.cpp
        src/SchedulerPolicy.cpp
        src/ProviderPolicy.cpp
        src/TransportPolicy.cpp
        src/PipelinePolicy.cpp
        src/CompatibilityDatabase.cpp
        src/AsyncOverlapEstimator.cpp
        src/AsyncQualification.cpp
        src/AutoTuneCoordinator.cpp
        src/GuideValidation.cpp
        src/MotionConfidence.cpp
        src/TemporalHistoryRegistry.cpp
        src/CrossQueueClockCalibrator.cpp
        src/ResidualPolicy.cpp
        src/TemporalConfidence.cpp
        src/MgpuPlanner.cpp
        src/NvofPolicy.cpp
        src/ResidualReprojection.cpp
        src/ResidualEngine.cpp
        src/NrSession.cpp
        src/NrSessionWorkState.cpp
        src/FusionRuntimeTiming.cpp
        src/FusionRuntimeLifecycle.cpp
    )
    add_library(nrfusion_nr_session_portable STATIC
        ${NRFUSION_NR_SESSION_PORTABLE_SOURCES})
    target_include_directories(nrfusion_nr_session_portable PUBLIC include)
    if (MSVC)
        target_compile_options(nrfusion_nr_session_portable PRIVATE /W4 /permissive-)
    else()
        target_compile_options(nrfusion_nr_session_portable PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()

    function(nrfusion_nr_session_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE nrfusion_nr_session_portable)
        if (MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    nrfusion_nr_session_test(
        nrfusion_nr_session_tests tests/nr_session_tests.cpp)
    nrfusion_nr_session_test(
        nrfusion_nr_session_stress_tests tests/nr_session_stress_tests.cpp)
else()
    nrfusion_test(nrfusion_nr_session_tests tests/nr_session_tests.cpp)
    nrfusion_test(nrfusion_nr_session_stress_tests tests/nr_session_stress_tests.cpp)
endif()

nrfusion_test(nrfusion_tests tests/controller_tests.cpp)
target_sources(nrfusion_tests PRIVATE src/OptiScalerAdapter.cpp)
nrfusion_test(nrfusion_game_probe_tests tests/game_probe_tests.cpp)
nrfusion_test(nrfusion_telemetry_tests tests/telemetry_tracker_tests.cpp)
nrfusion_test(nrfusion_timing_mapper_tests tests/timing_mapper_tests.cpp)
nrfusion_test(nrfusion_nr_timing_source_tests tests/nr_timing_source_tests.cpp)
nrfusion_test(nrfusion_runtime_shell_tests tests/runtime_shell_tests.cpp)
nrfusion_test(nrfusion_ngx_feature_registry_tests tests/ngx_feature_registry_tests.cpp)
nrfusion_test(nrfusion_submission_gate_tests tests/submission_gate_tests.cpp)
nrfusion_test(nrfusion_nr_retirement_queue_tests tests/nr_retirement_queue_tests.cpp)
nrfusion_test(nrfusion_d3d11_bridge_slot_tracker_tests tests/d3d11_bridge_slot_tracker_tests.cpp)
nrfusion_test(nrfusion_d3d11_carrier_work_tests tests/d3d11_carrier_work_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_contract_tests tests/d3d12_carrier_contract_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_session_tests tests/d3d12_carrier_session_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_native_facts_tests tests/d3d12_carrier_native_facts_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_capabilities_tests tests/d3d12_carrier_capabilities_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_bootstrap_tests tests/d3d12_carrier_bootstrap_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_work_tests tests/d3d12_carrier_work_tests.cpp)
nrfusion_test(nrfusion_d3d12_carrier_execution_plan_tests tests/d3d12_carrier_execution_plan_tests.cpp)
nrfusion_test(nrfusion_d3d12_guide_format_tests tests/d3d12_guide_format_tests.cpp)

add_executable(nrfusion_d3d12_nr_frame_plan_tests
    tests/d3d12_nr_frame_plan_tests.cpp
    src/D3D12NrFramePlan.cpp
)
target_include_directories(nrfusion_d3d12_nr_frame_plan_tests PRIVATE include)
if (MSVC)
    target_compile_options(nrfusion_d3d12_nr_frame_plan_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_d3d12_nr_frame_plan_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_d3d12_nr_frame_plan_tests
    COMMAND nrfusion_d3d12_nr_frame_plan_tests)

nrfusion_test(nrfusion_vulkan_carrier_contract_tests tests/vulkan_carrier_contract_tests.cpp)

nrfusion_test(nrfusion_vulkan_carrier_acquire_tests tests/vulkan_carrier_acquire_tests.cpp)

nrfusion_test(nrfusion_vulkan_carrier_session_tests tests/vulkan_carrier_session_tests.cpp)
