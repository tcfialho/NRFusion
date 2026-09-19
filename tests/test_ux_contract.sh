#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
installer="$root/installer/NRFusion.nsi"
patcher="$root/tools/apply_to_optiscaler.py"

# Zero manual INI workflow: Setup may only enable the NR master switch on first install.
if grep 'WriteINIStr.*OptiScaler.ini' "$installer" | grep -v '"DlssNr" "Enabled" "true"' >/dev/null; then
  echo "FAIL: installer writes tuning INI keys" >&2
  exit 1
fi
grep -q 'WriteINIStr.*OptiScaler.ini.*"DlssNr" "Enabled" "true"' "$installer"
if grep -q 'ModePage\|Performance mode\|Precision.*page\|Proxy.*page' "$installer"; then
  echo "FAIL: installer asks the user to choose technical/performance configuration" >&2
  exit 1
fi

grep -q 'Page custom GamePageCreate GamePageLeave' "$installer"
grep -q 'nsDialogs::SelectFileDialog' "$installer"
grep -q -- '--api-exit-code' "$installer"
grep -q -- '--support-exit-code' "$installer"
grep -q '32-bit.*not ready yet' "$installer"
grep -q 'OpenGL.*not ready yet' "$installer"
grep -q 'Nothing was overwritten' "$installer"
grep -q -- '--verify-sha256' "$installer"
grep -q -- '--snapshot-install' "$installer"
grep -q -- '--restore-install' "$installer"

# Four entries: three adaptive contracts that differ only in the floor, plus the manual escape.
grep -q 'static const char\* fusionModes\[\].*Auto.*Best quality.*Performance.*Custom' "$patcher"
grep -q 'legacyBalancedMode' "$patcher"
# Custom owns the percentage and has no target to chase; the adaptive modes own the target.
grep -q 'SliderInt("Neural resolution"' "$patcher"
if ! grep -q 'fusionCustomMode' "$patcher"; then
  echo "FAIL: the menu no longer distinguishes the manual mode" >&2
  exit 1
fi
# Both sliders commit when the handle is released. Committing per pixel of a drag rebuilds the
# model dozens of times a second, and a target change restarts the controller's ladder.
grep -q 'fusionPendingTargetFps' "$patcher"
grep -q 'IsItemDeactivatedAfterEdit' "$patcher"
grep -q 'fusionAutoPrecision = fusionPrecisionMode != 3 && fusionPrecisionMode != 4' "$patcher"
grep -q 'fusionHybridBackendIntegrated = false' "$patcher"
grep -q 'fusionCaps.asyncCompute = false' "$patcher"
grep -q 'fusionCaps.secondaryGpu = false' "$patcher"
grep -q 'manual NVFP4 is unavailable on this GPU/runtime' "$patcher"
grep -q 'AdaRuntime' "$patcher"
grep -q 'FindAdaRuntimeSidecar' "$patcher"
grep -q 'Ada FP8 sidecar' "$patcher"
grep -q 'g_resolveTime' "$patcher"
grep -q 'fusionResolveTimingActive' "$patcher"
grep -q 'ms resolve' "$patcher"
grep -q 'FrameTimingSource::PresentationInterval' "$patcher"
grep -q 'SliderFloat("Target FPS"' "$patcher"
grep -q 'Adjusting: NR' "$patcher"
grep -q 'Stable: NR' "$patcher"
grep -q 'Pipeline protected:' "$patcher"
grep -q 'External pressure:' "$patcher"
grep -q 'fusionCustomMode && ImGui::TreeNode("Advanced controls")' "$patcher"

# No hidden NRFusion tuning knobs behind the simple UI. Custom means the upstream controls are authoritative.
if grep -q 'DlssNrAdaptiveMinScale\|DlssNrAdaptiveMaxScale\|DlssNrTargetBudgetMs\|DlssNrPressureGovernor' "$patcher"; then
  echo "FAIL: hidden NRFusion tuning knobs remain in host integration" >&2
  exit 1
fi
grep -q 'fusionSettings.enabled = fusionSettings.mode != nrfusion::UserMode::Custom' "$patcher"
grep -q 'DlssNrRunBeforeSr.set_volatile_value(true)' "$patcher"
grep -q 'DlssNrPasses.set_volatile_value(1u)' "$patcher"

grep -q 'baseline backups are tied to this original name' "$installer"
echo "NRFusion zero-INI NSIS UX contract passed"
