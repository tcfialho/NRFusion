if (WIN32)
    find_package(CUDAToolkit 13.4 QUIET)
    if (CUDAToolkit_FOUND AND MSVC)
        set(nr_ffn_cubin "${CMAKE_CURRENT_BINARY_DIR}/generated/nrfusion_ffn_reference_sm89.cubin")
        add_custom_command(OUTPUT "${nr_ffn_cubin}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
            COMMAND "${CUDAToolkit_NVCC_EXECUTABLE}" -std=c++20 -arch=sm_89 --cubin
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cuda/NrFfnExactReference.cu"
                -o "${nr_ffn_cubin}" -ccbin "${CMAKE_CXX_COMPILER}"
            DEPENDS src/cuda/NrFfnExactReference.cu VERBATIM)
        add_custom_target(nrfusion_ffn_reference DEPENDS "${nr_ffn_cubin}")
    endif()
    add_executable(nrfusion_kernel_replay
        tools/nr_replay/main.cpp tools/nr_replay/ReplayPacket.cpp
        tools/nr_replay/ReplayDevice.cpp tools/nr_replay/ReplayStock.cpp tools/nr_replay/ReplayBenchmark.cpp
        tools/nr_replay/ReplayChainPacket.cpp tools/nr_replay/ReplayChain.cpp tools/nr_replay/ReplayChainBenchmark.cpp
        tools/nr_replay/ReplaySwinPacket.cpp tools/nr_replay/ReplaySwin.cpp
        src/Sha256.cpp src/NrKernelArchitecture.cpp)
    target_include_directories(nrfusion_kernel_replay PRIVATE include src tools/nr_replay
        "${nrfusion_nvapi_source_SOURCE_DIR}")
    target_link_libraries(nrfusion_kernel_replay PRIVATE d3d12 dxgi)
    set_target_properties(nrfusion_kernel_replay PROPERTIES OUTPUT_NAME "NRFusionKernelReplay")
    if (MSVC)
        target_compile_options(nrfusion_kernel_replay PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
