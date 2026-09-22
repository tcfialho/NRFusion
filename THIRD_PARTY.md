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

NRFusion's public source/developer distribution does not include NVIDIA's proprietary `nvngx_dlssnr.dll` runtime. `tools/build_dist.ps1` can optionally accept a user-supplied, hash-approved runtime to create a private/self-contained installer; that local input is not part of this repository.


## Imported DLSS-NR composition shader

- `shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl` is vendored verbatim from
  `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass@1b1dd650d35ea59ea2d1d0bf7937b71645159f75`,
  Git blob `4a6102820f736e9349ffed370259d094f2a7f4ae`.
- That upstream is GPL-3.0; NRFusion is also GPL-3.0.
- The shader contains colour-composition work derived from RenoDX by clshortfuse under MIT.
  The required attribution and MIT text are preserved in `licenses/RenoDX_ATTRIBUTION.txt`.
- Generated `DlssNr_Shader.cso` and `DlssNr_Shader.h` are not committed. The generator verifies
  their locked Git blob IDs before accepting them.
