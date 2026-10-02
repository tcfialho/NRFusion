#include "nrfusion/Diagnostics.hpp"

#include <iomanip>
#include <sstream>

namespace nrfusion {
namespace {

const char* ApiName(GraphicsApi value) noexcept {
    switch (value) {
    case GraphicsApi::D3D9: return "d3d9";
    case GraphicsApi::D3D10: return "d3d10";
    case GraphicsApi::D3D11: return "d3d11";
    case GraphicsApi::D3D12: return "d3d12";
    case GraphicsApi::Vulkan: return "vulkan";
    case GraphicsApi::OpenGL: return "opengl";
    default: return "unknown";
    }
}

const char* ProviderName(FrameProvider value) noexcept {
    switch (value) {
    case FrameProvider::Native: return "native";
    case FrameProvider::Bridge: return "bridge";
    case FrameProvider::Synthetic: return "synthetic";
    default: return "unsupported";
    }
}

const char* PlacementName(NrPlacement value) noexcept {
    switch (value) {
    case NrPlacement::PreSr: return "pre-sr";
    case NrPlacement::DeferredResidual: return "deferred-residual";
    case NrPlacement::AcrossRr: return "across-rr";
    case NrPlacement::PostSr: return "post-sr";
    default: return "auto";
    }
}

const char* SchedulerName(SchedulerMode value) noexcept {
    switch (value) {
    case SchedulerMode::Serialized: return "serialized";
    case SchedulerMode::AsyncCompute: return "async";
    case SchedulerMode::SecondaryGpu: return "secondary-gpu";
    default: return "auto";
    }
}

const char* MotionName(MotionSource value) noexcept {
    switch (value) {
    case MotionSource::Native: return "native";
    case MotionSource::DlssContract: return "dlss-contract";
    case MotionSource::NvidiaOpticalFlow: return "nvof";
    case MotionSource::ShaderEstimated: return "shader";
    default: return "zero";
    }
}

const char* AdaptiveStateName(AdaptiveState value) noexcept {
    switch (value) {
    case AdaptiveState::WaitingForTiming: return "waiting-for-timing";
    case AdaptiveState::Stable: return "stable";
    case AdaptiveState::ReducingScale: return "reducing-scale";
    case AdaptiveState::ExternalPressure: return "external-pressure";
    case AdaptiveState::RecoveringQuality: return "recovering-quality";
    default: return "unknown";
    }
}


const char* AsyncStateName(AsyncQualificationState value) noexcept {
    switch (value) {
    case AsyncQualificationState::NeedSerializedBaseline: return "need-serialized-baseline";
    case AsyncQualificationState::ReadyForTrial: return "ready-for-trial";
    case AsyncQualificationState::CollectingAsyncTrial: return "collecting-async-trial";
    case AsyncQualificationState::PreferSerialized: return "prefer-serialized";
    case AsyncQualificationState::PreferAsync: return "prefer-async";
    default: return "unknown";
    }
}

const char* PrecisionStateName(PrecisionAutotuneState value) noexcept {
    switch (value) {
    case PrecisionAutotuneState::BaselineWarmup: return "baseline-warmup";
    case PrecisionAutotuneState::BaselineMeasure: return "baseline-measure";
    case PrecisionAutotuneState::CandidateWarmup: return "candidate-warmup";
    case PrecisionAutotuneState::CandidateMeasure: return "candidate-measure";
    case PrecisionAutotuneState::PreferFp8: return "prefer-fp8";
    case PrecisionAutotuneState::PreferNvfp4: return "prefer-nvfp4";
    case PrecisionAutotuneState::CandidateFailed: return "candidate-failed";
    default: return "unknown";
    }
}
const char* PresentationName(PresentationMode value) noexcept {
    switch (value) {
    case PresentationMode::FrameGeneration: return "fg";
    case PresentationMode::MultiFrameGeneration: return "mfg";
    default: return "none";
    }
}

const char* FrameTimingSourceName(FrameTimingSource value) noexcept {
    switch (value) {
    case FrameTimingSource::GpuTimestamp: return "gpu-timestamp";
    case FrameTimingSource::PresentationInterval: return "presentation-interval";
    default: return "unknown";
    }
}

const char* ReasonName(DecisionReason value) noexcept {
    switch (value) {
    case DecisionReason::PipelineUnsupported: return "pipeline-unsupported";
    case DecisionReason::ScaleChanged: return "scale-changed";
    case DecisionReason::SourceGovernorActive: return "source-governor-active";
    case DecisionReason::AsyncSelected: return "async-selected";
    case DecisionReason::AsyncRejected: return "async-rejected";
    case DecisionReason::SecondaryGpuSelected: return "secondary-gpu-selected";
    case DecisionReason::MotionFallback: return "motion-fallback";
    case DecisionReason::PrecisionCandidateSelected: return "precision-candidate-selected";
    case DecisionReason::PresentationActive: return "presentation-active";
    default: return "stable";
    }
}

} // namespace

std::string Diagnostics::ToText(const DiagnosticsSnapshot& s) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "NR Fusion diagnostics\n";
    out << "frame=" << s.frameId << " reason=" << ReasonName(s.reason)
        << " supported=" << (s.decision.supported ? 1 : 0)
        << " pipelineSupported=" << (s.decision.pipeline.supported ? 1 : 0) << '\n';
    out << "api=" << ApiName(s.game.api)
        << " provider=" << ProviderName(s.decision.pipeline.provider)
        << " transport=" << TransportName(s.decision.pipeline.transport)
        << " placement=" << PlacementName(s.decision.pipeline.placement)
        << " scheduler=" << SchedulerName(s.decision.scheduler)
        << " motion=" << MotionName(s.decision.pipeline.motion)
        << " precision=" << PrecisionName(s.decision.precision) << '\n';
    out << "scale=" << Finite(static_cast<double>(s.decision.workingScale))
        << " sourceCap=" << Finite(s.decision.sourceCapFps)
        << " presentation=" << PresentationName(s.decision.presentation)
        << " multiplier=" << s.decision.generationMultiplier << '\n';
    out << "adaptiveTelemetryReady=" << (s.decision.performance.telemetryReady ? 1 : 0)
        << " gpuFrameTiming=" << (s.decision.performance.gpuFrameTimingAvailable ? 1 : 0)
        << " adaptiveState=" << AdaptiveStateName(s.decision.performance.state) << '\n';
    out << "nrMs=" << Finite(s.telemetry.nrGpuMs)
        << " nrTimingFresh=" << (s.telemetry.nrTimingFresh ? 1 : 0)
        << " frameMs=" << Finite(s.telemetry.frameGpuMs)
        << " frameTiming=" << FrameTimingSourceName(s.telemetry.frameTimingSource)
        << " sourceFps=" << Finite(s.telemetry.sourceFps)
        << " processedFps=" << Finite(s.telemetry.processedFps)
        << " queue=" << Finite(s.telemetry.queuePressure)
        << " asyncOverlap=" << Finite(s.telemetry.asyncOverlap) << '\n';
    out << "capabilities="
        << " native=" << (s.capabilities.nativeProvider ? 1 : 0)
        << " bridge=" << (s.capabilities.bridgeProvider ? 1 : 0)
        << " syntheticD3D12=" << (s.capabilities.syntheticD3D12 ? 1 : 0)
        << " syntheticD3D11=" << (s.capabilities.syntheticD3D11Bridge ? 1 : 0)
        << " syntheticVulkan=" << (s.capabilities.syntheticVulkan ? 1 : 0)
        << " openGl=" << (s.capabilities.openGlCarrier ? 1 : 0)
        << " x86=" << (s.capabilities.x86Carrier ? 1 : 0)
        << " preSr=" << (s.capabilities.preSr ? 1 : 0)
        << " deferred=" << (s.capabilities.deferredResidual ? 1 : 0)
        << " acrossRr=" << (s.capabilities.acrossRr ? 1 : 0)
        << " async=" << (s.capabilities.asyncCompute ? 1 : 0)
        << " gpu2=" << (s.capabilities.secondaryGpu ? 1 : 0)
        << " fp8=" << (s.capabilities.fp8 ? 1 : 0)
        << " nvfp4=" << (s.capabilities.hybridNvfp4 ? 1 : 0) << '\n';
    out << "asyncState=" << AsyncStateName(s.asyncState)
        << " precisionState=" << PrecisionStateName(s.precisionState) << '\n';
    out << "historyGeneration=" << s.historyGeneration
        << " scaleGeneration=" << s.scaleGeneration
        << " droppedResiduals=" << s.droppedResiduals
        << " motionConfidence=" << Finite(s.motionConfidence)
        << " disocclusion=" << Finite(s.disocclusionRatio) << '\n';
    return out.str();
}

std::string Diagnostics::ToJson(const DiagnosticsSnapshot& s) {
    std::ostringstream out;
    out << std::setprecision(9);
    out << '{'
        << "\"frame\":" << s.frameId
        << ",\"reason\":\"" << ReasonName(s.reason) << '"'
        << ",\"supported\":" << (s.decision.supported ? "true" : "false")
        << ",\"pipelineSupported\":" << (s.decision.pipeline.supported ? "true" : "false")
        << ",\"api\":\"" << ApiName(s.game.api) << '"'
        << ",\"game32Bit\":" << (s.game.is32Bit ? "true" : "false")
        << ",\"nativeDlss\":" << (s.game.nativeDlss ? "true" : "false")
        << ",\"rayReconstruction\":" << (s.game.rayReconstruction ? "true" : "false")
        << ",\"frameGenerationAvailable\":" << (s.game.frameGeneration ? "true" : "false")
        << ",\"provider\":\"" << ProviderName(s.decision.pipeline.provider) << '"'
        << ",\"transport\":\"" << TransportName(s.decision.pipeline.transport) << '"'
        << ",\"placement\":\"" << PlacementName(s.decision.pipeline.placement) << '"'
        << ",\"scheduler\":\"" << SchedulerName(s.decision.scheduler) << '"'
        << ",\"motion\":\"" << MotionName(s.decision.pipeline.motion) << '"'
        << ",\"precision\":\"" << PrecisionName(s.decision.precision) << '"'
        << ",\"workingScale\":" << Finite(static_cast<double>(s.decision.workingScale))
        << ",\"sourceCapFps\":" << Finite(s.decision.sourceCapFps)
        << ",\"presentation\":\"" << PresentationName(s.decision.presentation) << '"'
        << ",\"generationMultiplier\":" << s.decision.generationMultiplier
        << ",\"adaptiveTelemetryReady\":" << (s.decision.performance.telemetryReady ? "true" : "false")
        << ",\"gpuFrameTimingAvailable\":" << (s.decision.performance.gpuFrameTimingAvailable ? "true" : "false")
        << ",\"adaptiveState\":\"" << AdaptiveStateName(s.decision.performance.state) << '"'
        << ",\"nrGpuMs\":" << Finite(s.telemetry.nrGpuMs)
        << ",\"nrTimingFresh\":" << (s.telemetry.nrTimingFresh ? "true" : "false")
        << ",\"frameGpuMs\":" << Finite(s.telemetry.frameGpuMs)
        << ",\"frameTimingSource\":\"" << FrameTimingSourceName(s.telemetry.frameTimingSource) << '"'
        << ",\"sourceFps\":" << Finite(s.telemetry.sourceFps)
        << ",\"processedFps\":" << Finite(s.telemetry.processedFps)
        << ",\"queuePressure\":" << Finite(s.telemetry.queuePressure)
        << ",\"asyncOverlap\":" << Finite(s.telemetry.asyncOverlap)
        << ",\"crossAdapterMs\":" << Finite(s.telemetry.crossAdapterMs)
        << ",\"asyncState\":\"" << AsyncStateName(s.asyncState) << '"'
        << ",\"precisionState\":\"" << PrecisionStateName(s.precisionState) << '"'
        << ",\"capNativeProvider\":" << (s.capabilities.nativeProvider ? "true" : "false")
        << ",\"capBridgeProvider\":" << (s.capabilities.bridgeProvider ? "true" : "false")
        << ",\"capSyntheticD3D12\":" << (s.capabilities.syntheticD3D12 ? "true" : "false")
        << ",\"capSyntheticD3D11Bridge\":" << (s.capabilities.syntheticD3D11Bridge ? "true" : "false")
        << ",\"capSyntheticVulkan\":" << (s.capabilities.syntheticVulkan ? "true" : "false")
        << ",\"capOpenGlCarrier\":" << (s.capabilities.openGlCarrier ? "true" : "false")
        << ",\"capX86Carrier\":" << (s.capabilities.x86Carrier ? "true" : "false")
        << ",\"capPreSr\":" << (s.capabilities.preSr ? "true" : "false")
        << ",\"capDeferredResidual\":" << (s.capabilities.deferredResidual ? "true" : "false")
        << ",\"capAcrossRr\":" << (s.capabilities.acrossRr ? "true" : "false")
        << ",\"capPostSr\":" << (s.capabilities.postSr ? "true" : "false")
        << ",\"capAsyncCompute\":" << (s.capabilities.asyncCompute ? "true" : "false")
        << ",\"capSecondaryGpu\":" << (s.capabilities.secondaryGpu ? "true" : "false")
        << ",\"capNativeMotion\":" << (s.capabilities.nativeMotion ? "true" : "false")
        << ",\"capDlssContractMotion\":" << (s.capabilities.dlssContractMotion ? "true" : "false")
        << ",\"capNvof\":" << (s.capabilities.nvof ? "true" : "false")
        << ",\"capShaderMotion\":" << (s.capabilities.shaderMotion ? "true" : "false")
        << ",\"capFp8\":" << (s.capabilities.fp8 ? "true" : "false")
        << ",\"capHybridNvfp4\":" << (s.capabilities.hybridNvfp4 ? "true" : "false")
        << ",\"capFrameGeneration\":" << (s.capabilities.frameGeneration ? "true" : "false")
        << ",\"capMaxGenerationMultiplier\":" << static_cast<unsigned>(s.capabilities.maxGenerationMultiplier)
        << ",\"historyGeneration\":" << s.historyGeneration
        << ",\"scaleGeneration\":" << s.scaleGeneration
        << ",\"droppedResiduals\":" << s.droppedResiduals
        << ",\"motionConfidence\":" << Finite(s.motionConfidence)
        << ",\"disocclusionRatio\":" << Finite(s.disocclusionRatio)
        << '}';
    return out.str();
}

} // namespace nrfusion
