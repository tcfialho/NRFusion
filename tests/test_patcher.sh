#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp -R "$ROOT/tests/fixture/." "$TMP/"
python3 -m py_compile "$ROOT/tools/apply_to_optiscaler.py"
python3 "$ROOT/tools/apply_to_optiscaler.py" "$TMP" > /dev/null
# Applying the patcher to its own output must be a no-op, not a duplicate-config/anchor failure.
python3 "$ROOT/tools/apply_to_optiscaler.py" "$TMP" > /dev/null
grep -q 'DlssNrFusionMode' "$TMP/OptiScaler/Config.h"
grep -q 'DlssNrAdaRuntime' "$TMP/OptiScaler/Config.h"
grep -q 'DlssNrAdaRuntime.set_from_config' "$TMP/OptiScaler/Config.cpp"
grep -q 'ResolveWorkingScale' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'ConsumeTrackedNrGpuMs' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'ReportQualityEvidence' "$TMP/OptiScaler/nrfusion/OptiScalerAdapter.hpp"
grep -q 'BeginNrWork' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'ResetForShape' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'ReportWorkingScaleBuildFailure' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'precision changed to.*rebuilding the neural feature' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'PrecisionCandidateRequested()' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'DlssNrNative::HybridAvailable()' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'NVFP4 Auto unavailable' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'fusionHybridBackendIntegrated = false' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'fusionCaps.asyncCompute = false' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'fusionCaps.secondaryGpu = false' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'manual NVFP4 is unavailable on this GPU/runtime' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'fusionStoredMode == 3' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'fusionStoredMode == 3' "$TMP/OptiScaler/dlssnr/DlssNrFeature_Vk.cpp"
# A pinned precision must never be written over by Auto, and the second precision must be reported
# as a capability only where the hardware has it.
grep -q '!fusionPinnedPrecision && cfg.DlssNrPrecision' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'DlssNrPrecisionManual' "$TMP/OptiScaler/Config.h"
grep -q 'NV_GPU_ARCHITECTURE_AD100' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
# The hybrid precision is a capability only when its assets verify, not when the GPU
# merely could run it. Otherwise Auto picks a precision that fails at create time.
grep -q 'hardware && DlssNrNative::HybridAvailable()' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'Hybrid NVFP4 nao esta disponivel' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'ImGui::Combo("Precision"' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
# The menu must read the device's precisions from the adapter, not re-detect the GPU itself.
grep -q 'SupportedPrecisions()' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
# Kernel timing has to reach the menu, and its sources have to reach the host project.
grep -q 'NrKernelProfiler::Instance()' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'include <nrfusion/NrKernelProfile.hpp>' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
test -f "$TMP/OptiScaler/nrfusion/NrKernelProfile.cpp"
grep -q 'NrKernelProfile.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
# Written out rather than as `! grep`: the shell exempts a negated command from set -e,
# so every negative assertion written that way passes even when it should fail. This one
# did, and a regression reached a release build because of it.
if grep -q 'IdentifyGpu' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"; then
    echo "o menu voltou a detectar a GPU sozinho; ele deve perguntar ao adaptador" >&2
    exit 1
fi
grep -q 'bool HybridAvailable();' "$TMP/OptiScaler/dlssnr/DlssNrNative.h"
grep -q 'VerifyAssets();available=true' "$TMP/OptiScaler/dlssnr/DlssNrNative.cpp"
grep -q 'RetireTimedInterval' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'UpdateD3D12QueueClock' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'QueryPerformanceFrequency' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q '_trigger\[_currentFrameIndex\] = false' "$TMP/OptiScaler/gpu_time/GpuTime_Dx12.cpp"
grep -q 'Max FPS' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'Advanced controls' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'Copy diagnostics' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'SetClipboardText' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'renderedFps=' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'displayedFps=' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'precisionBackend=fp8-ada-validated' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'capability.hybridNvfp4=0' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -Fq 'nrfusion\TemporalConfidence.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\MgpuPlanner.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\NvofPolicy.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\TelemetryTracker.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\GuideValidation.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\MotionNormalization.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\MotionConfidence.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\TemporalHistoryRegistry.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\MotionGuideBinding.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
test -f "$TMP/OptiScaler/nrfusion/MotionGuideSelection.hpp"
test -f "$TMP/OptiScaler/nrfusion/GuideHistoryState.hpp"
test -f "$TMP/OptiScaler/nrfusion/MotionGuideBinding.hpp"
grep -Fq 'nrfusion\PipelinedExecutorState.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\WorkLedger.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\TimingWorkMapper.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\AsyncOverlapEstimator.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
test -f "$TMP/OptiScaler/nrfusion/D3D12QueueClockBridge.hpp"
test -f "$TMP/OptiScaler/nrfusion/D3D12AsyncFenceBridge.hpp"
grep -Fq 'nrfusion\PrecisionAutotuner.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\AutoTuneCoordinator.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\ResidualEngine.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\ProfileStore.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\CompatibilityDatabase.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\Diagnostics.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\QualityValidator.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
test -f "$TMP/OptiScaler/nrfusion/AutoDecision.hpp"
test -f "$TMP/OptiScaler/nrfusion/QualityValidator.hpp"
test -f "$TMP/OptiScaler/nrfusion/RuntimeCapabilities.hpp"
test -f "$TMP/OptiScaler/nrfusion/FrameContractProvider.hpp"
test -f "$TMP/OptiScaler/nrfusion/CompatibilityDatabase.hpp"
test -f "$TMP/OptiScaler/nrfusion/ResidualEngine.hpp"
# Verify the flattened host copy is dependency-closed. This catches a new FusionRuntime include
# being added without adding its header/source to the patcher manifest.
python3 - "$TMP/OptiScaler/nrfusion" <<'PYDEP'
from pathlib import Path
import re, sys
root = Path(sys.argv[1])
missing = []
for path in root.glob('*'):
    if path.suffix not in {'.hpp', '.cpp'}:
        continue
    for inc in re.findall(r'^#include "([^"/]+\.(?:hpp|h))"', path.read_text(encoding='utf-8'), re.M):
        if not (root / inc).exists():
            missing.append(f"{path.name}: {inc}")
if missing:
    raise SystemExit("missing flattened includes: " + "; ".join(missing))
PYDEP
# The Microsoft compiler when it is available, because it is the one that actually builds the
# host and it rejects things gcc accepts -- a windows.h min/max macro collision reached a
# release build once precisely because only gcc had seen the file.
if command -v cl >/dev/null 2>&1; then
    for cpp in "$TMP"/OptiScaler/nrfusion/*.cpp; do
        cl //nologo //c //EHsc //std:c++20 //W4 //I "$TMP/OptiScaler/nrfusion" "$cpp" //Fo:"$TMP/msvc.obj" >/dev/null
    done
fi
# Linux syntax-check of flattened sources that are actually portable. Win32/D3D integration
# remains covered by the MSVC pass above and by Windows validation.
for cpp in "$TMP"/OptiScaler/nrfusion/*.cpp; do
    case "$cpp" in
        *AdaW4A8Interceptor.cpp|*DlssgTransfusion.cpp|*SyntheticDx12Provider.cpp|\
        *NvofMotionProvider.cpp|*SyntheticDx11BridgeProvider.cpp|*SyntheticVulkanProvider.cpp|\
        *NrD3D12Diagnostics.cpp)
            continue
            ;;
    esac
    g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I "$TMP/OptiScaler/nrfusion" -fsyntax-only "$cpp"
done
# The diagnostic ABI is dead weight unless the patcher installs it, compiles it into the host
# and wires it into the real NVAPI launch chain the testbed reads back.
test -f "$TMP/OptiScaler/nrfusion/NrD3D12Diagnostics.cpp"
test -f "$TMP/OptiScaler/nrfusion/NrDiagnosticsApi.hpp"
grep -Fq 'nrfusion\NrD3D12Diagnostics.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -q 'NRFusion_BeginNrDiagnosticFrame' "$TMP/OptiScaler/nrfusion/NrD3D12Diagnostics.cpp"
grep -q 'nrfusion::NoteNrFunction' "$TMP/OptiScaler/dlssnr/DlssNrNative.cpp"
grep -q 'nrfusion::ForgetNrFunction' "$TMP/OptiScaler/dlssnr/DlssNrNative.cpp"
grep -q 'nrfusion::ProfileNrChain' "$TMP/OptiScaler/dlssnr/DlssNrNative.cpp"
if grep -q 's.launch(c,k,count)' "$TMP/OptiScaler/dlssnr/DlssNrNative.cpp"; then
  echo "FAIL: a pass-through to the original NVAPI launch escaped the diagnostic wrapper" >&2
  exit 1
fi
grep -q 'NrPassDiagnosticScope nrDiagnostic' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -q 'nrDiagnostic.Succeeded()' "$TMP/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
grep -Fq 'nrfusion\FrameLimitPolicy.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -Fq 'nrfusion\ResidualReprojection.cpp' "$TMP/OptiScaler/OptiScaler.vcxproj"
grep -q 'FrameLimitPolicy::Resolve' "$TMP/OptiScaler/misc/FrameLimit.cpp"
grep -q 'fusionMaxFpsBilinear' "$TMP/OptiScaler/framegen/dlssg/AmpereMfgLoader.cpp"
grep -q 'DlssNrFusionMode.value_or_default() == 1' "$TMP/OptiScaler/framegen/dlssg/AmpereMfgLoader.cpp"
if grep -q 'AdaptiveScale=' "$TMP/OptiScaler.ini"; then
    echo "OptiScaler.ini nao deve expor AdaptiveScale" >&2
    exit 1
fi
# DLSS 5 Neural Rendering visual-tuning section: present, independent of the Mode/Custom gate,
# never exposes an internal NR/model preset or Local Tone, and disables/tooltips per-field on
# unsupported capability instead of assuming support.
grep -q 'DLSS 5 Neural Rendering' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'nrfusion::ResolveDlss5NeuralRendering' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'Not supported by current DLSS runtime' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
grep -q 'dlss5nr.style.requested=' "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp"
python3 - "$TMP/OptiScaler/dlssnr/DlssNr_Menu.cpp" <<'PYDLSS5'
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
start = text.index('DLSS 5 Neural Rendering')
end = text.index('const auto fusionDecision', start)
section = text[start:end]
for forbidden in ('DlssNrPreset', 'Model Preset', 'NR Preset', 'Local Tone', 'LocalTone'):
    if forbidden in section:
        raise SystemExit(f"DLSS 5 NR section must not expose {forbidden!r}")
PYDLSS5
# The rebuilt testbed must keep the flags and keys the old one had, or the scripts that drove
# it stop working for reasons that have nothing to do with what they are testing.
grep -q -- '--fixed-scene' "$ROOT/tools/requiem_game/main.cpp"
grep -q -- '--deterministic-motion' "$ROOT/tools/requiem_game/main.cpp"
grep -q 'D3D12 Testbed' "$ROOT/tools/requiem_game/main.cpp"
grep -q 'NVSDK_NGX_D3D12_EvaluateFeature' "$ROOT/tools/requiem_game/ngx_dlss.cpp"
# It is a comparator: both reference frames have to ship with it.
test -f "$ROOT/tools/requiem_game/assets/nvidia-dlss5-requiem-off.jpeg"
test -f "$ROOT/tools/requiem_game/assets/nvidia-dlss5-requiem-on.jpeg"
echo 'patcher fixture test passed'
