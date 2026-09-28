#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
package="$root/tools/package_standalone_dist.ps1"
installer="$root/installer/NRFusion.nsi"
sections="$root/installer/NRFusionSections.nsh"

grep -q 'ForwarderPath' "$package"
grep -q 'ForwarderSha256' "$package"
grep -q 'RuntimePath' "$package"
grep -q 'THIRD_PARTY.md' "$package"
grep -q 'licenses' "$package"
grep -q 'nrfusion_proxy.dll' "$package"
grep -q 'NRFusion\\internal\\NRFusionHost64.exe' "$package"
grep -q 'NRFusion\\internal\\nvngx.dll_dlssnr.dll' "$package"
grep -q 'NRFusion\\internal\\nvngx_dlssnr.dll' "$package"
if grep -q 'CacheDist\|nrfusion_capture32.dll\|OptiScaler.ini' "$package"; then
  echo "FAIL: standalone package retains an obsolete payload dependency" >&2
  exit 1
fi

cmake_version="$(sed -n 's/^project(NRFusion VERSION \([^ ]*\).*/\1/p' "$root/CMakeLists.txt")"
installer_version="$(sed -n 's/^!define APP_VERSION "\([^"]*\)"/\1/p' "$installer")"
if [[ -z "$cmake_version" || "$installer_version" != "$cmake_version" ]]; then
  echo "FAIL: installer APP_VERSION ($installer_version) does not match CMake project version ($cmake_version)" >&2
  exit 1
fi

grep -q '^Unicode true' "$installer"
grep -q '^RequestExecutionLevel admin' "$installer"
grep -q 'SetCompressor /SOLID lzma' "$installer"
grep -q 'nsDialogs::SelectFileDialog' "$installer"
grep -q -- '--snapshot-install' "$sections"
grep -q -- '--record-install' "$sections"
grep -q -- '--restore-install' "$sections"
grep -q 'NRFusionHost64.exe.*NRFusion\\internal' "$sections"
grep -q 'nvngx.dll_dlssnr.dll.*NRFusion\\internal' "$sections"
grep -q 'nvngx_dlssnr.dll.*NRFusion\\internal' "$sections"
grep -q 'File /oname=games.json "..\\dist\\NRFusion\\compat\\games.json"' "$sections"
if grep -q 'nrfusion_capture32\|OptiScaler.ini\|OptiScaler.dll' "$sections"; then
  echo "FAIL: installer retains a payload that is not in the standalone manifest" >&2
  exit 1
fi

echo "NRFusion standalone distribution contract passed"
