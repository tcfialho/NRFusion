cmake_minimum_required(VERSION 3.20)
project(NRFusion VERSION 0.5.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(nrfusion_core STATIC
    src/PerformanceController.cpp
    src/NrCostModel.cpp
    src/WorkLedger.cpp
    src/TimingWorkMapper.cpp
    src/AsyncOverlapEstimator.cpp
    src/CrossQueueClockCalibrator.cpp
    src/AsyncQualification.cpp
    src/PrecisionAutotuner.cpp
    src/AutoTuneCoordinator.cpp
    src/ProfileStore.cpp
    src/CompatibilityDatabase.cpp
    src/Diagnostics.cpp
    src/FrameLimitPolicy.cpp
    src/ResidualReprojection.cpp
    src/ResidualEngine.cpp
    src/SchedulerPolicy.cpp
    src/ProviderPolicy.cpp
    src/TransportPolicy.cpp
    src/PipelinePolicy.cpp
    src/ResidualPolicy.cpp
    src/TemporalConfidence.cpp
    src/MgpuPlanner.cpp
    src/NvofPolicy.cpp
    src/NvofWrapper.cpp
    src/TelemetryTracker.cpp
    src/GuideValidation.cpp
    src/MotionNormalization.cpp
    src/MotionConfidence.cpp
    src/TemporalHistoryRegistry.cpp
    src/PipelinedExecutorState.cpp
    src/NrKernelProfile.cpp
    src/GameProbe.cpp
    src/Sha256.cpp
    src/InstallerState.cpp
    src/OptiScalerAdapter.cpp
    src/Presets.cpp
    src/QualityValidator.cpp
    src/Dlss5NeuralRendering.cpp
    src/AdaptiveExposure.cpp
    src/AdaptiveExposureController.cpp
)

if (WIN32)
    target_sources(nrfusion_core PRIVATE
        src/AdaW4A8Interceptor.cpp
        src/DlssgTransfusion.cpp
        src/SyntheticDx12Provider.cpp
        src/NvofMotionProvider.cpp
        src/SyntheticDx11BridgeProvider.cpp
        src/CaptureProvider32.cpp
        src/CaptureProvider32Export.cpp
        src/HostServer64.cpp
        src/HostDlssNr.cpp
        src/SyntheticVulkanProvider.cpp
        src/SyntheticOpenGlProvider.cpp
    )
endif()

# The fused grouped feed-forward has a CUDA backend when a CUDA compiler is present, and a
# stub that reports NoDevice when it is not. Callers ask the interface either way, so no
# consumer has to know which one was built.
include(CheckLanguage)
check_language(CUDA)
if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 89)   # Ada; the graph's tile layout targets it
    endif()
    set_target_properties(nrfusion_core PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
    target_sources(nrfusion_core PRIVATE src/cuda/FusedGroupedFfn.cu src/cuda/W4A8FfnSm89.cu)
    target_compile_definitions(nrfusion_core PUBLIC NRFUSION_HAS_CUDA=1)
else()
    target_sources(nrfusion_core PRIVATE src/FusedGroupedFfnStub.cpp src/W4A8FfnStub.cpp)
endif()

target_include_directories(nrfusion_core PUBLIC include)
target_compile_definitions(nrfusion_core PUBLIC NRFUSION_CAPTURE32_STATIC=1)
if (MSVC)
    target_compile_options(nrfusion_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(nrfusion_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Werror>)
endif()

add_executable(nrfusion_tests tests/controller_tests.cpp)
target_link_libraries(nrfusion_tests PRIVATE nrfusion_core)
if (MSVC)
    target_compile_options(nrfusion_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(nrfusion_tests PRIVATE -UNDEBUG)
endif()

add_executable(nrfusion_game_probe_tests tests/game_probe_tests.cpp)
target_link_libraries(nrfusion_game_probe_tests PRIVATE nrfusion_core)
if (MSVC)
    target_compile_options(nrfusion_game_probe_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(nrfusion_game_probe_tests PRIVATE -UNDEBUG)
endif()

add_executable(nrfusion_telemetry_tests tests/telemetry_tracker_tests.cpp)
target_link_libraries(nrfusion_telemetry_tests PRIVATE nrfusion_core)
if (MSVC)
    target_compile_options(nrfusion_telemetry_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(nrfusion_telemetry_tests PRIVATE -UNDEBUG)
endif()

add_executable(nrfusion_timing_mapper_tests tests/timing_mapper_tests.cpp)
target_link_libraries(nrfusion_timing_mapper_tests PRIVATE nrfusion_core)
if (MSVC)
    target_compile_options(nrfusion_timing_mapper_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(nrfusion_timing_mapper_tests PRIVATE -UNDEBUG)
endif()

add_executable(nrfusion_probe tools/game_probe_main.cpp)
target_link_libraries(nrfusion_probe PRIVATE nrfusion_core)
set_target_properties(nrfusion_probe PROPERTIES OUTPUT_NAME "NRFusionProbe")

enable_testing()
add_test(NAME nrfusion_tests COMMAND nrfusion_tests)
add_test(NAME nrfusion_game_probe_tests COMMAND nrfusion_game_probe_tests)
add_test(NAME nrfusion_telemetry_tests COMMAND nrfusion_telemetry_tests)
add_test(NAME nrfusion_timing_mapper_tests COMMAND nrfusion_timing_mapper_tests)

add_executable(nrfusion_sim tests/simulation.cpp)
target_link_libraries(nrfusion_sim PRIVATE nrfusion_core)

if (WIN32)
    add_executable(nrfusion_harness_3d
        tests/harness_3d/D3D12TestHarness.hpp
        tests/harness_3d/D3D12TestHarness.cpp
        tests/harness_3d/main.cpp
    )
    target_link_libraries(nrfusion_harness_3d PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    target_include_directories(nrfusion_harness_3d PRIVATE tests/harness_3d)
    add_test(NAME nrfusion_harness_3d COMMAND nrfusion_harness_3d --headless --frames 60)

    add_executable(nrfusion_residual_gpu_test tests/residual_gpu_test.cpp)
    target_link_libraries(nrfusion_residual_gpu_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_residual_gpu_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_residual_gpu_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_residual_gpu_test COMMAND nrfusion_residual_gpu_test)

    add_executable(nrfusion_synthetic_dx12_test tests/synthetic_dx12_test.cpp)
    target_link_libraries(nrfusion_synthetic_dx12_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_synthetic_dx12_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_synthetic_dx12_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_synthetic_dx12_test COMMAND nrfusion_synthetic_dx12_test)

    add_executable(nrfusion_synthetic_dx12_scale_gate_test tests/synthetic_dx12_scale_gate_test.cpp)
    target_link_libraries(nrfusion_synthetic_dx12_scale_gate_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_synthetic_dx12_scale_gate_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_synthetic_dx12_scale_gate_test PRIVATE -UNDEBUG)
    endif()
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_test(NAME nrfusion_synthetic_dx12_scale_gate_test COMMAND nrfusion_synthetic_dx12_scale_gate_test)
    endif()

    add_executable(nrfusion_synthetic_dx11_bridge_test tests/synthetic_dx11_bridge_test.cpp)
    target_link_libraries(nrfusion_synthetic_dx11_bridge_test PRIVATE nrfusion_core d3d11 d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_synthetic_dx11_bridge_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_synthetic_dx11_bridge_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_synthetic_dx11_bridge_test COMMAND nrfusion_synthetic_dx11_bridge_test)

    add_executable(nrfusion_synthetic_opengl_test tests/synthetic_opengl_test.cpp)
    target_link_libraries(nrfusion_synthetic_opengl_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler opengl32)
    if (MSVC)
        target_compile_options(nrfusion_synthetic_opengl_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_synthetic_opengl_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_synthetic_opengl_test COMMAND nrfusion_synthetic_opengl_test)

    add_executable(nrfusion_host64 host/NRFusionHost64.cpp)
    target_link_libraries(nrfusion_host64 PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    set_target_properties(nrfusion_host64 PROPERTIES OUTPUT_NAME "NRFusionHost64")

    add_executable(nrfusion_ipc_host_test tests/ipc_host_test.cpp)
    target_link_libraries(nrfusion_ipc_host_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_ipc_host_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_ipc_host_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_ipc_host_test COMMAND nrfusion_ipc_host_test)

    add_library(nrfusion_capture32 SHARED
        src/CaptureProvider32.cpp
        src/CaptureProvider32Export.cpp
        src/CaptureD3D11.cpp
        src/VersionDllProxy.cpp
        src/VersionDllProxy.def
    )
    target_include_directories(nrfusion_capture32 PUBLIC include)
    target_compile_definitions(nrfusion_capture32 PRIVATE NRFUSION_CAPTURE32_EXPORTS=1)
    target_link_libraries(nrfusion_capture32 PRIVATE d3d11 dxgi d3dcompiler)
    set_target_properties(nrfusion_capture32 PROPERTIES OUTPUT_NAME "nrfusion_capture32")

    add_executable(nrfusion_capture32_roundtrip_test tests/capture32_roundtrip_test.cpp)
    target_link_libraries(nrfusion_capture32_roundtrip_test PRIVATE nrfusion_capture32 d3d11 dxgi)
    add_dependencies(nrfusion_capture32_roundtrip_test nrfusion_host64)
    if (MSVC)
        target_compile_options(nrfusion_capture32_roundtrip_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_capture32_roundtrip_test PRIVATE -UNDEBUG)
    endif()
    # The CTest registration uses a same-architecture host binary. The Win32 executable is run
    # explicitly against the x64 host in the Windows validation command, so it cannot accidentally
    # report a same-bitness pass as x86-carrier coverage.
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_test(NAME nrfusion_capture32_roundtrip_test COMMAND nrfusion_capture32_roundtrip_test)
    endif()

    add_executable(nrfusion_capture32_d3d11_hook_test tests/capture32_d3d11_hook_test.cpp)
    target_link_libraries(nrfusion_capture32_d3d11_hook_test PRIVATE nrfusion_capture32 d3d11 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_capture32_d3d11_hook_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_capture32_d3d11_hook_test PRIVATE -UNDEBUG)
    endif()



    # requiem_game was rebuilt on 14/09/2026 after the original was lost; requiem_viewer's
    # source is still gone. Each target is gated on its own file, so one being absent no longer
    # takes the other down with it.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/requiem_viewer/main.cpp")
        add_executable(nrfusion_requiem_viewer tools/requiem_viewer/main.cpp)
        target_link_libraries(nrfusion_requiem_viewer PRIVATE d3d11 dxgi d3dcompiler windowscodecs ole32)
    endif()

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/requiem_game/main.cpp")
        add_executable(nrfusion_requiem_game tools/requiem_game/main.cpp
                                            tools/requiem_game/ngx_dlss.cpp
                                            tools/requiem_game/image.cpp)
        # windowscodecs and ole32 decode the reference frames, as the original linked them too.
        target_link_libraries(nrfusion_requiem_game PRIVATE d3d12 dxgi d3dcompiler windowscodecs ole32)
        target_include_directories(nrfusion_requiem_game PRIVATE include)
        add_custom_command(TARGET nrfusion_requiem_game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/requiem_game/assets"
                    "$<TARGET_FILE_DIR:nrfusion_requiem_game>/assets")
        set_target_properties(nrfusion_requiem_game PROPERTIES OUTPUT_NAME "RequiemGame")
    endif()
endif()
