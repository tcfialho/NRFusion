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
