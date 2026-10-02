#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
installer="$root/installer/NRFusion.nsi"
sections="$root/installer/NRFusionSections.nsh"

grep -q 'Page custom GamePageCreate GamePageLeave' "$installer"
grep -q 'nsDialogs::SelectFileDialog' "$installer"
grep -q -- '--api-exit-code' "$installer"
grep -q -- '--support-exit-code' "$installer"
grep -q 'D3D12 requires native DLSS' "$installer"
grep -q '!insertmacro TryProxy "version.dll"' "$installer"
grep -q '!insertmacro TryProxy "dxgi.dll"' "$installer"
grep -q -- '--proxy-exit-code' "$installer"
if grep -q 'TryProxy "winmm.dll"\|TryProxy "dbghelp.dll"' "$installer"; then
  echo "FAIL: installer can rename the version proxy to an incompatible loader name" >&2
  exit 1
fi
if grep -q 'WriteINIStr.*OptiScaler.ini\|OptiScaler.ini' "$sections"; then
  echo "FAIL: standalone installer still writes OptiScaler configuration" >&2
  exit 1
fi
grep -q 'Transport" "d3d11-host64' "$sections"
grep -q 'NRFusionHost64.exe' "$sections"
grep -q 'nvngx.dll_dlssnr.dll' "$sections"
grep -q 'nvngx_dlssnr.dll' "$sections"

echo "NRFusion standalone installer UX contract passed"
