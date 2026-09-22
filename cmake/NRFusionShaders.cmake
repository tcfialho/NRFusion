find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(NRFUSION_FXC_EXECUTABLE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstreams/wilsjo/OptiScaler/shaders/shader_tools/fxc.exe"
    CACHE FILEPATH "Locked upstream fxc.exe used for DLSS-NR DXBC reproduction")

set(NRFUSION_DLSSNR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/dlssnr")
set(NRFUSION_DLSSNR_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl")
set(NRFUSION_DLSSNR_RESIDUAL_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vendor/optiscaler_dlssnr/dlssnr_residual.hlsl")

set(NRFUSION_DLSSNR_SHADER_CSO "${NRFUSION_DLSSNR_GENERATED_DIR}/DlssNr_Shader.cso")
set(NRFUSION_DLSSNR_SHADER_HEADER "${NRFUSION_DLSSNR_GENERATED_DIR}/DlssNr_Shader.h")
set(NRFUSION_DLSSNR_RESIDUAL_CSO
    "${NRFUSION_DLSSNR_GENERATED_DIR}/dlssnr_residual_Shader.cso")
set(NRFUSION_DLSSNR_RESIDUAL_HEADER
    "${NRFUSION_DLSSNR_GENERATED_DIR}/dlssnr_residual_Shader.h")

add_custom_command(
    OUTPUT
        "${NRFUSION_DLSSNR_SHADER_CSO}"
        "${NRFUSION_DLSSNR_SHADER_HEADER}"
        "${NRFUSION_DLSSNR_RESIDUAL_CSO}"
        "${NRFUSION_DLSSNR_RESIDUAL_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NRFUSION_DLSSNR_GENERATED_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dlssnr_shader.py"
            --variant main
            --source "${NRFUSION_DLSSNR_SHADER_SOURCE}"
            --output-dir "${NRFUSION_DLSSNR_GENERATED_DIR}"
            --fxc "${NRFUSION_FXC_EXECUTABLE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dlssnr_shader.py"
            --variant residual
            --source "${NRFUSION_DLSSNR_RESIDUAL_SOURCE}"
            --output-dir "${NRFUSION_DLSSNR_GENERATED_DIR}"
            --fxc "${NRFUSION_FXC_EXECUTABLE}"
    DEPENDS
        "${NRFUSION_DLSSNR_SHADER_SOURCE}"
        "${NRFUSION_DLSSNR_RESIDUAL_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dlssnr_shader.py"
    VERBATIM
)

add_custom_target(nrfusion_dlssnr_shader_codegen
    DEPENDS
        "${NRFUSION_DLSSNR_SHADER_CSO}"
        "${NRFUSION_DLSSNR_SHADER_HEADER}"
        "${NRFUSION_DLSSNR_RESIDUAL_CSO}"
        "${NRFUSION_DLSSNR_RESIDUAL_HEADER}")
