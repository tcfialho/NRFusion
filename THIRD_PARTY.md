# Research / upstream references

NRFusion contains original orchestration code and integrates with selected upstream components at build time.
Primary build integration currently targets:

- wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass (GPL-3.0)

Design/implementation research also tracks:

- matiasLombo/neural-upstream
- kibblerz/DLSS5-Reshade-AIO
- maohgad-web/Neural-coprocessor
- jlrouzies-fr/DLSS5-Feeder
- NIGos/dlss5-bridge

Preserve each upstream project's license/NOTICE when code is actually imported rather than independently reimplemented.

NRFusion's public source/developer distribution does not include NVIDIA's proprietary `nvngx_dlssnr.dll` runtime. The standalone Host64 also requires an `nvngx.dll_dlssnr.dll` sidecar, but this checkout does not contain the code that produces that binary. `tools/build_dist.ps1` accepts approved sidecars for a self-contained installer.


## Imported DLSS-NR composition shader

- `shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl` is vendored verbatim from
  `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass@1b1dd650d35ea59ea2d1d0bf7937b71645159f75`,
  Git blob `4a6102820f736e9349ffed370259d094f2a7f4ae`.
- That upstream is GPL-3.0; NRFusion is also GPL-3.0.
- The shader contains colour-composition work derived from RenoDX by clshortfuse under MIT.
  The required attribution and MIT text are preserved in `licenses/RenoDX_ATTRIBUTION.txt`.
- Generated `DlssNr_Shader.cso` and `DlssNr_Shader.h` are not committed. The generator verifies
  their locked Git blob IDs before accepting them.
# NVAPI SDK headers

NRFusion uses the NVIDIA NVAPI SDK headers at commit `70d337db9186e968eab622f7e786de7e437faf3d`.
They declare the driver interfaces observed by the optional neural kernel profiler.
The driver DLL remains supplied by NVIDIA; NRFusion does not distribute an NVAPI static library.
SDK header license: `licenses/NVAPI_LICENSE.txt` (MIT).
