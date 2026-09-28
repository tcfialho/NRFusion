add_library(nrfusion_d3d11_carrier_slice STATIC
    src/SyntheticDx12ProviderLifecycle.cpp
    src/SyntheticDx12ProviderAccounting.cpp
    src/SyntheticDx12ProviderSubmit.cpp
    src/SyntheticDx12ProviderResidual.cpp
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
