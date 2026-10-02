include(FetchContent)
FetchContent_Declare(nrfusion_streamline_source
    URL https://codeload.github.com/NVIDIAGameWorks/Streamline/zip/refs/tags/v2.12.0
    URL_HASH SHA256=47db9aa532516f10408b03c59274c9321153b10c8ca1bbb2f8e742cea81420fa
    SOURCE_SUBDIR nrfusion_headers_only
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nrfusion_streamline_source)
FetchContent_Declare(nrfusion_minhook_source
    URL https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4
    URL_HASH SHA256=172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nrfusion_minhook_source)
FetchContent_Declare(nrfusion_nvapi_source
    URL https://codeload.github.com/NVIDIA/nvapi/zip/70d337db9186e968eab622f7e786de7e437faf3d
    URL_HASH SHA256=fa979b5d8d5115a106a24d331806c9b0a573035043fec441a6579ccfc2b5669b
    SOURCE_SUBDIR nrfusion_headers_only
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nrfusion_nvapi_source)
target_sources(nrfusion_core PRIVATE src/NrKernelNvapiHooks.cpp src/NrKernelProfileD3D12.cpp
    src/NrKernelProfileExport.cpp src/NrKernelProfileStatistics.cpp
    src/NrKernelRuntimeIdentity.cpp src/NrKernelAbiDiscovery.cpp src/NrKernelPointerObservation.cpp
    src/NrKernelResourceObservation.cpp src/NrKernelResourceTargets.c
    src/NrKernelCapture.cpp src/NrKernelCaptureExport.cpp
    src/NrKernelChainCapture.cpp src/NrKernelChainCaptureExport.cpp
    src/NrSwinKernelCapture.cpp src/NrSwinKernelCaptureExport.cpp
    src/NrKernelReplacement.cpp src/NrKernelArchitecture.cpp)
set_property(SOURCE src/NrKernelResourceObservation.cpp APPEND
    PROPERTY INCLUDE_DIRECTORIES "${nrfusion_minhook_source_SOURCE_DIR}/include")
set_property(SOURCE src/NrKernelNvapiHooks.cpp src/NrKernelReplacement.cpp APPEND
    PROPERTY INCLUDE_DIRECTORIES "${nrfusion_nvapi_source_SOURCE_DIR}")
add_library(nrfusion_overlay_cursor STATIC src/RuntimeOverlayCursor.cpp)
target_include_directories(nrfusion_overlay_cursor PRIVATE include src)
target_link_libraries(nrfusion_overlay_cursor PRIVATE minhook user32)
target_link_libraries(nrfusion_core PRIVATE nrfusion_overlay_cursor minhook)
target_sources(nrfusion_core PRIVATE src/StreamlineDlssgOptions.cpp)
set_property(SOURCE src/StreamlineDlssgHook.cpp src/StreamlineDlssgOptions.cpp
    src/NrKernelDriverDiscovery.cpp APPEND
    PROPERTY INCLUDE_DIRECTORIES "${nrfusion_minhook_source_SOURCE_DIR}/include"
                                "${nrfusion_streamline_source_SOURCE_DIR}/include")
FetchContent_Declare(nrfusion_imgui_source
    URL https://codeload.github.com/ocornut/imgui/zip/refs/tags/v1.91.9b
    URL_HASH SHA256=fd37507c8476a6d14cc7c4b352401f31bcbd0f0d995d35390811e968c466f46e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nrfusion_imgui_source)
set(imgui_root "${nrfusion_imgui_source_SOURCE_DIR}")
add_library(nrfusion_imgui STATIC
    "${imgui_root}/imgui.cpp"
    "${imgui_root}/imgui_draw.cpp"
    "${imgui_root}/imgui_tables.cpp"
    "${imgui_root}/imgui_widgets.cpp"
    "${imgui_root}/backends/imgui_impl_dx12.cpp"
    "${imgui_root}/backends/imgui_impl_win32.cpp")
target_include_directories(nrfusion_imgui PUBLIC "${imgui_root}" "${imgui_root}/backends")
target_link_libraries(nrfusion_imgui PUBLIC d3d12 d3dcompiler dxgi dwmapi)
target_sources(nrfusion_core PRIVATE
    src/RuntimeOverlayD3D12.cpp
    src/RuntimeOverlayD3D12Input.cpp
    src/RuntimeOverlayD3D12Metrics.cpp
    src/RuntimeOverlayImGui.cpp)
target_sources(nrfusion_core PRIVATE src/RuntimeOverlayImGuiAdvanced.cpp)
target_link_libraries(nrfusion_core PUBLIC nrfusion_imgui)
