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

    add_library(nrfusion_proxy SHARED
        src/Proxy64Export.cpp
        src/VersionDllProxy.cpp
        src/VersionDllProxy.def
        src/NgxGameProxyD3D12.cpp
        src/NgxGameProxyDiagnostics.cpp
        src/CaptureProvider32.cpp
        src/CaptureProvider32Io.cpp
        src/CaptureProvider32Frames.cpp
        src/CaptureProvider32Export.cpp
        src/CaptureD3D11.cpp
    )
    target_include_directories(nrfusion_proxy PUBLIC include)
    target_compile_definitions(nrfusion_proxy PRIVATE NRFUSION_CAPTURE32_EXPORTS=1)
    target_compile_options(nrfusion_proxy PRIVATE /UNRFUSION_CAPTURE32_STATIC)
    target_link_libraries(nrfusion_proxy PRIVATE nrfusion_core d3d11 d3d12 dxgi d3dcompiler user32 gdi32)
    set_target_properties(nrfusion_proxy PROPERTIES OUTPUT_NAME "nrfusion_proxy")

    add_executable(nrfusion_proxy_runtime_tests tests/proxy64_runtime_tests.cpp)
    add_dependencies(nrfusion_proxy_runtime_tests nrfusion_proxy)
    add_test(NAME nrfusion_proxy_runtime_tests COMMAND nrfusion_proxy_runtime_tests)
    set_tests_properties(nrfusion_proxy_runtime_tests PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:nrfusion_proxy>")
endif()
