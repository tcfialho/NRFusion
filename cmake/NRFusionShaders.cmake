set(NRFUSION_FXC_EXECUTABLE
    "${CMAKE_CURRENT_SOURCE_DIR}/upstreams/wilsjo/OptiScaler/shaders/shader_tools/fxc.exe"
    CACHE FILEPATH "Locked upstream fxc.exe used for DLSS-NR DXBC reproduction")

set(NRFUSION_DLSSNR_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl")
set(NRFUSION_DLSSNR_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/dlssnr")
set(NRFUSION_DLSSNR_SHADER_CSO
    "${NRFUSION_DLSSNR_GENERATED_DIR}/DlssNr_Shader.cso")
set(NRFUSION_DLSSNR_SHADER_HEADER
    "${NRFUSION_DLSSNR_GENERATED_DIR}/DlssNr_Shader.h")

add_custom_command(
    OUTPUT "${NRFUSION_DLSSNR_SHADER_CSO}" "${NRFUSION_DLSSNR_SHADER_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NRFUSION_DLSSNR_GENERATED_DIR}"
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dlssnr_shader.py"
            --source "${NRFUSION_DLSSNR_SHADER_SOURCE}"
            --output-dir "${NRFUSION_DLSSNR_GENERATED_DIR}"
            --fxc "${NRFUSION_FXC_EXECUTABLE}"
    DEPENDS
        "${NRFUSION_DLSSNR_SHADER_SOURCE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dlssnr_shader.py"
    VERBATIM
)

add_custom_target(nrfusion_dlssnr_shader_codegen
    DEPENDS "${NRFUSION_DLSSNR_SHADER_CSO}" "${NRFUSION_DLSSNR_SHADER_HEADER}")
