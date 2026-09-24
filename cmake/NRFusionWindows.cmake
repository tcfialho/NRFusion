add_executable(nrfusion_harness_3d
        tests/harness_3d/D3D12TestHarness.hpp
        tests/harness_3d/D3D12HarnessLifecycle.cpp
        tests/harness_3d/D3D12HarnessResources.cpp
        tests/harness_3d/D3D12HarnessShaders.cpp
        tests/harness_3d/D3D12HarnessPipeline.cpp
        tests/harness_3d/D3D12HarnessScene.cpp
        tests/harness_3d/D3D12HarnessExecution.cpp
        tests/harness_3d/D3D12HarnessProvider.cpp
        tests/harness_3d/D3D12HarnessDispatch.cpp
        tests/harness_3d/D3D12HarnessRun.cpp
        tests/harness_3d/D3D12HarnessCorrectness.cpp
        tests/harness_3d/D3D12HarnessScenario.cpp
        tests/harness_3d/D3D12HarnessBenchmark.cpp
        tests/harness_3d/D3D12HarnessMetrics.cpp
        tests/harness_3d/main.cpp
    )
    target_link_libraries(nrfusion_harness_3d PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    target_include_directories(nrfusion_harness_3d PRIVATE tests/harness_3d)
    add_test(NAME nrfusion_harness_3d COMMAND nrfusion_harness_3d --headless --frames 120)
    add_test(NAME nrfusion_harness_3d_scenarios COMMAND nrfusion_harness_3d --headless --scenario all)
    add_test(NAME nrfusion_harness_3d_benchmark
             COMMAND nrfusion_harness_3d --headless --benchmark --benchmark-iterations 10000)
    add_test(NAME nrfusion_harness_3d_cli_unknown COMMAND nrfusion_harness_3d --scenaro all)
    set_tests_properties(nrfusion_harness_3d_cli_unknown PROPERTIES WILL_FAIL TRUE)
    add_test(NAME nrfusion_harness_3d_cli_conflict
             COMMAND nrfusion_harness_3d --benchmark --scenario all)
    set_tests_properties(nrfusion_harness_3d_cli_conflict PROPERTIES WILL_FAIL TRUE)
    add_test(NAME nrfusion_harness_3d_cli_zero_frames
             COMMAND nrfusion_harness_3d --no-async --frames 0)
    set_tests_properties(nrfusion_harness_3d_cli_zero_frames PROPERTIES WILL_FAIL TRUE)

    add_executable(nrfusion_nr_scratch_resources_tests
        tests/d3d12_nr_scratch_resources_tests.cpp
        src/D3D12NrScratchResources.cpp
        src/NrDeferredRetirementQueue.cpp
    )
    target_include_directories(nrfusion_nr_scratch_resources_tests PRIVATE include)
    target_link_libraries(nrfusion_nr_scratch_resources_tests PRIVATE d3d12 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_nr_scratch_resources_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_nr_scratch_resources_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_nr_scratch_resources_tests COMMAND nrfusion_nr_scratch_resources_tests)

    add_executable(nrfusion_nr_guide_clones_tests
        tests/d3d12_nr_guide_clones_tests.cpp
        src/D3D12NrGuideClones.cpp
        src/NrDeferredRetirementQueue.cpp
    )
    target_include_directories(nrfusion_nr_guide_clones_tests PRIVATE include)
    target_link_libraries(nrfusion_nr_guide_clones_tests PRIVATE d3d12 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_nr_guide_clones_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_nr_guide_clones_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_nr_guide_clones_tests COMMAND nrfusion_nr_guide_clones_tests)

    add_executable(nrfusion_d3d12_nr_codec_tests
        tests/d3d12_nr_codec_tests.cpp
        src/D3D12NrCodecInit.cpp
        src/D3D12NrCodecDispatch.cpp
    )
    add_dependencies(nrfusion_d3d12_nr_codec_tests nrfusion_dlssnr_shader_codegen)
    target_include_directories(nrfusion_d3d12_nr_codec_tests PRIVATE
        include
        "${NRFUSION_DLSSNR_GENERATED_DIR}"
    )
    target_link_libraries(nrfusion_d3d12_nr_codec_tests PRIVATE d3d12 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_d3d12_nr_codec_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_d3d12_nr_codec_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_d3d12_nr_codec_tests COMMAND nrfusion_d3d12_nr_codec_tests)

    add_executable(nrfusion_d3d12_retired_timing_source_tests
        tests/d3d12_retired_timing_source_tests.cpp)
    target_include_directories(nrfusion_d3d12_retired_timing_source_tests PRIVATE tests)
    target_link_libraries(nrfusion_d3d12_retired_timing_source_tests PRIVATE nrfusion_core d3d12 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_d3d12_retired_timing_source_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_d3d12_retired_timing_source_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_d3d12_retired_timing_source_tests
        COMMAND nrfusion_d3d12_retired_timing_source_tests)
    set_tests_properties(nrfusion_d3d12_retired_timing_source_tests PROPERTIES
        SKIP_RETURN_CODE 77)

    add_executable(nrfusion_residual_gpu_test tests/residual_gpu_test.cpp)
    target_link_libraries(nrfusion_residual_gpu_test PRIVATE nrfusion_core d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_residual_gpu_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_residual_gpu_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_residual_gpu_test COMMAND nrfusion_residual_gpu_test)
    set_tests_properties(nrfusion_residual_gpu_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(nrfusion_synthetic_dx12_test tests/synthetic_dx12_test.cpp)
    target_include_directories(nrfusion_synthetic_dx12_test PRIVATE tests)
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

    add_library(nrfusion_d3d11_carrier_slice STATIC
        src/SyntheticDx12Provider.cpp
        src/NvofMotionProvider.cpp
        src/SyntheticDx11BridgeProvider.cpp
        src/SyntheticDx11BridgeResources.cpp
        src/D3D11D3D12FenceBridge.cpp
        src/D3D11BridgeResources.cpp
        src/D3D11BridgeSlotTracker.cpp
        src/D3D11CarrierNativeAcquire.cpp
        src/D3D11CarrierWork.cpp
    )
    target_include_directories(nrfusion_d3d11_carrier_slice PUBLIC include)
    target_link_libraries(nrfusion_d3d11_carrier_slice PUBLIC
        d3d11 d3d12 dxgi d3dcompiler)
    if (MSVC)
        target_compile_options(nrfusion_d3d11_carrier_slice PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(nrfusion_d3d11_carrier_slice PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_executable(nrfusion_d3d11_carrier_hook_tests
        tests/d3d11_carrier_hook_tests.cpp
        src/D3D11CarrierHook.cpp
        src/D3D11CarrierNativeAcquire.cpp)
    target_include_directories(nrfusion_d3d11_carrier_hook_tests PRIVATE include)
    target_link_libraries(nrfusion_d3d11_carrier_hook_tests PRIVATE d3d11 dxgi)
    if (MSVC)
        target_compile_options(nrfusion_d3d11_carrier_hook_tests PRIVATE
            /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_d3d11_carrier_hook_tests PRIVATE -UNDEBUG)
    endif()
    add_executable(nrfusion_d3d11_carrier_native_acquire_tests
        tests/d3d11_carrier_native_acquire_tests.cpp)
    target_link_libraries(nrfusion_d3d11_carrier_native_acquire_tests PRIVATE nrfusion_d3d11_carrier_slice)
    if (MSVC)
        target_compile_options(nrfusion_d3d11_carrier_native_acquire_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_d3d11_carrier_native_acquire_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_d3d11_carrier_native_acquire_tests
        COMMAND nrfusion_d3d11_carrier_native_acquire_tests)
    add_executable(nrfusion_synthetic_dx11_bridge_test tests/synthetic_dx11_bridge_test.cpp)
    target_link_libraries(nrfusion_synthetic_dx11_bridge_test PRIVATE nrfusion_d3d11_carrier_slice)
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

    add_library(nrfusion_ipc_host_compile OBJECT
        src/HostServer64.cpp
        src/HostServer64Lifecycle.cpp
        src/HostServer64Transport.cpp
        src/HostServer64Guides.cpp
        src/HostServer64Frame.cpp)
    target_include_directories(nrfusion_ipc_host_compile PRIVATE include)
    if (MSVC)
        target_compile_options(nrfusion_ipc_host_compile PRIVATE /W4 /permissive-)
    else()
        target_compile_options(nrfusion_ipc_host_compile PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_executable(nrfusion_ipc_generation_contract_tests
        tests/ipc_generation_contract_tests.cpp)
    target_include_directories(nrfusion_ipc_generation_contract_tests PRIVATE include)
    if (MSVC)
        target_compile_options(nrfusion_ipc_generation_contract_tests PRIVATE /W4 /permissive- /UNDEBUG)
    else()
        target_compile_options(nrfusion_ipc_generation_contract_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME nrfusion_ipc_generation_contract_tests
        COMMAND nrfusion_ipc_generation_contract_tests)

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
        src/CaptureProvider32Frames.cpp
        src/CaptureProvider32Export.cpp
        src/CaptureD3D11.cpp
        src/VersionDllProxy.cpp
        src/VersionDllProxy.def
    )
    target_include_directories(nrfusion_capture32 PUBLIC include)
    target_compile_definitions(nrfusion_capture32 PRIVATE NRFUSION_CAPTURE32_EXPORTS=1)
    target_link_libraries(nrfusion_capture32 PRIVATE d3d11 dxgi d3dcompiler)
    set_target_properties(nrfusion_capture32 PROPERTIES OUTPUT_NAME "nrfusion_capture32")

    add_executable(nrfusion_capture32_roundtrip_test
        tests/capture32_roundtrip_test.cpp
        tests/Capture32RoundtripSupport.cpp
        tests/Capture32RoundtripNeural.cpp
        tests/Capture32RoundtripReduced.cpp)
    target_link_libraries(nrfusion_capture32_roundtrip_test PRIVATE nrfusion_capture32 d3d11 dxgi)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_dependencies(nrfusion_capture32_roundtrip_test nrfusion_host64)
    endif()
    if (MSVC)
        target_compile_options(nrfusion_capture32_roundtrip_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(nrfusion_capture32_roundtrip_test PRIVATE -UNDEBUG)
    endif()
    # Register only the x64 CTest path; Win32 cross-bitness coverage is run explicitly.
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

    # Gate each Requiem tool independently so one missing source does not hide the other.
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
