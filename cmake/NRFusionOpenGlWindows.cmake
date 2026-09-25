add_executable(nrfusion_synthetic_opengl_test
    tests/synthetic_opengl_test.cpp)
target_link_libraries(nrfusion_synthetic_opengl_test PRIVATE
    nrfusion_core d3d12 dxgi d3dcompiler opengl32)
if (MSVC)
    target_compile_options(nrfusion_synthetic_opengl_test PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_synthetic_opengl_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_synthetic_opengl_test
    COMMAND nrfusion_synthetic_opengl_test)

add_executable(nrfusion_opengl_external_interop_tests
    tests/opengl_external_interop_tests.cpp
    tests/OpenGlExternalInteropHarnessContext.cpp
    tests/OpenGlExternalInteropHarnessRun.cpp)
target_include_directories(nrfusion_opengl_external_interop_tests PRIVATE
    include tests)
target_link_libraries(nrfusion_opengl_external_interop_tests PRIVATE
    nrfusion_core d3d12 dxgi d3dcompiler opengl32)
if (MSVC)
    target_compile_options(nrfusion_opengl_external_interop_tests PRIVATE
        /W4 /permissive- /UNDEBUG)
else()
    target_compile_options(nrfusion_opengl_external_interop_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endif()
add_test(NAME nrfusion_opengl_external_interop_tests
    COMMAND nrfusion_opengl_external_interop_tests)
set_tests_properties(nrfusion_opengl_external_interop_tests PROPERTIES
    SKIP_RETURN_CODE 77)
