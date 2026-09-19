#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
lock="$root/upstreams.lock.json"
build="$root/tools/build_dist.ps1"
installer="$root/installer/NRFusion.nsi"
workflow="$root/.github/workflows/windows-validation.yml"

python - <<'PY' "$lock"
import json, re, sys
obj=json.load(open(sys.argv[1], encoding='utf-8'))
x=obj['build']['optiscaler']
assert x['repository'].startswith('https://github.com/wilsjo2/')
assert re.fullmatch(r'[0-9a-fA-F]{40}', x['commit'])
PY

grep -q "cat-file -e" "$build"
grep -q "fetch.*--depth=1.*origin" "$build"
grep -q "apply_to_optiscaler.py" "$build"
grep -q "AdaRuntimePath" "$build"
grep -q "EnableAdaRuntime" "$build"
grep -q "CB1A1A6E47E38B9229DB4F22081C0F9B1271783599675E0AB13D41D4D039755A" "$build"
grep -q "OptiScaler\\\\dlssnr\\\\forwarder" "$build"
grep -q "Required runtime directory missing" "$build"
grep -q "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E" "$build"
grep -q "E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A" "$build"
grep -q "SHA256SUMS.txt" "$build"
grep -q "Find-MakeNSIS" "$build"
grep -q "makensis" "$build"
if grep -qi 'ISCC\|Inno Setup\|NRFusion\.iss' "$build"; then
  echo "FAIL: Inno dependency remains in build_dist" >&2
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
grep -q -- '--snapshot-install' "$installer"
grep -q -- '--record-install' "$installer"
grep -q -- '--restore-install' "$installer"
grep -q 'installed.sha256' "$installer"
grep -q 'ProxySHA256' "$installer"
grep -q 'NRFusionProbe.exe' "$installer"
grep -q 'WriteUninstaller' "$installer"
grep -q 'File /r "..\\dist\\OptiScaler\\\*\.\*"' "$installer"
grep -q 'File /nonfatal /r "..\\dist\\Licenses\\\*\.\*"' "$installer"
grep -q 'File /oname=games.json "..\\dist\\NRFusion\\compat\\games.json"' "$installer"
grep -q 'File /nonfatal /oname=nvngx_dlssnr_ada.dll "..\\dist\\nvngx_dlssnr_ada.dll"' "$installer"
grep -Fq "compat\\games.json" "$build"

grep -q 'choco install nsis' "$workflow"
if grep -qi 'innosetup\|Inno Setup' "$workflow"; then
  echo "FAIL: Inno dependency remains in Windows workflow" >&2
  exit 1
fi

echo "NRFusion NSIS distribution contract passed"
