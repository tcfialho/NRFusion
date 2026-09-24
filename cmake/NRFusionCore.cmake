add_library(nrfusion_core STATIC
    src/PerformanceController.cpp
    src/PerformanceControllerLifecycle.cpp
    src/PerformanceControllerScale.cpp
    src/FusionRuntimeTiming.cpp
    src/FusionRuntimeLifecycle.cpp
    src/NrCostModel.cpp
    src/WorkLedger.cpp
    src/TimingWorkMapper.cpp
    src/AsyncOverlapEstimator.cpp
    src/CrossQueueClockCalibrator.cpp
    src/AsyncQualification.cpp
    src/PrecisionAutotuner.cpp
    src/AutoTuneCoordinator.cpp
    src/ProfileStore.cpp
    src/CompatibilityDatabase.cpp
    src/Diagnostics.cpp
    src/FrameLimitPolicy.cpp
    src/ResidualReprojection.cpp
    src/ResidualEngine.cpp
    src/SchedulerPolicy.cpp
    src/ProviderPolicy.cpp
    src/TransportPolicy.cpp
    src/PipelinePolicy.cpp
    src/ResidualPolicy.cpp
    src/TemporalConfidence.cpp
    src/MgpuPlanner.cpp
    src/NvofPolicy.cpp
    src/NvofWrapper.cpp
    src/TelemetryTracker.cpp
    src/GuideValidation.cpp
    src/MotionNormalization.cpp
    src/MotionConfidence.cpp
    src/TemporalHistoryRegistry.cpp
    src/PipelinedExecutorState.cpp
    src/NrKernelProfile.cpp
    src/GameProbe.cpp
    src/GameProbeInspect.cpp
    src/GameProbeDetection.cpp
    src/GameProbeSupport.cpp
    src/Sha256.cpp
    src/InstallerState.cpp
    src/Presets.cpp
    src/QualityValidator.cpp
    src/Dlss5NeuralRendering.cpp
    src/AdaptiveExposure.cpp
    src/AdaptiveExposureController.cpp
    src/RuntimeShell.cpp
    src/RuntimeBootstrap.cpp
    src/NrSession.cpp
    src/NrSessionWorkState.cpp
    src/NgxFeatureRegistry.cpp
    src/NrSubmissionGate.cpp
    src/NrDeferredRetirementQueue.cpp
    src/D3D12NrFramePlan.cpp
    src/D3D12CarrierContract.cpp
    src/D3D12CarrierSession.cpp
    src/D3D12CarrierNativeFacts.cpp
    src/D3D12CarrierCapabilities.cpp
    src/D3D12CarrierBootstrap.cpp
    src/D3D12CarrierExecutionPlan.cpp
    src/D3D12GuideFormat.cpp
    src/D3D11BridgeSlotTracker.cpp
    src/D3D11CarrierWork.cpp
    src/VulkanCarrierContract.cpp
    src/VulkanCarrierAcquire.cpp
    src/VulkanCarrierSession.cpp
    src/VulkanCarrierExecutionPlan.cpp
    src/OpenGlCarrierAcquire.cpp
)

if (WIN32)
    target_sources(nrfusion_core PRIVATE
        src/AdaW4A8Interceptor.cpp
        src/DlssgTransfusion.cpp
        src/SyntheticDx12Provider.cpp
        src/NvofMotionProvider.cpp
        src/SyntheticDx11BridgeProvider.cpp
        src/SyntheticDx11BridgeResources.cpp
        src/D3D11D3D12FenceBridge.cpp
        src/D3D11BridgeResources.cpp
        src/D3D11CarrierNativeAcquire.cpp
        src/CaptureProvider32.cpp
        src/CaptureProvider32Io.cpp
        src/CaptureProvider32Frames.cpp
        src/CaptureProvider32Export.cpp
        src/HostServer64.cpp
        src/HostServer64Lifecycle.cpp
        src/HostServer64Transport.cpp
        src/HostServer64Guides.cpp
        src/HostServer64Frame.cpp
        src/D3D12NrExecutorLoader.cpp
        src/D3D12NrExecutorLifecycle.cpp
        src/D3D12NrExecutorDispatch.cpp
        src/D3D12NrExecutorPasses.cpp
        src/D3D12NrExecutorFrame.cpp
        src/D3D12NrExecutorFramePrepare.cpp
        src/D3D12NrExecutorFrameModel.cpp
        src/D3D12NrExecutorResidual.cpp
        src/D3D12NrScratchResources.cpp
        src/D3D12NrGuideClones.cpp
        src/D3D12NrCodecInit.cpp
        src/D3D12NrCodecDispatch.cpp
        src/D3D12CarrierNativeAcquire.cpp
        src/D3D12CarrierExecutor.cpp
        src/D3D12RetiredTimingSource.cpp
        src/SyntheticVulkanProvider.cpp
        src/SyntheticOpenGlProvider.cpp
        src/SyntheticOpenGlProviderLifecycle.cpp
        src/SyntheticOpenGlProviderInterop.cpp
    )
endif()

include(CheckLanguage)
check_language(CUDA)
if (CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 89)
    endif()
    set_target_properties(nrfusion_core PROPERTIES CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
    target_sources(nrfusion_core PRIVATE src/cuda/FusedGroupedFfn.cu src/cuda/W4A8FfnSm89.cu)
    target_compile_definitions(nrfusion_core PUBLIC NRFUSION_HAS_CUDA=1)
else()
    target_sources(nrfusion_core PRIVATE src/FusedGroupedFfnStub.cpp src/W4A8FfnStub.cpp)
endif()

target_include_directories(nrfusion_core PUBLIC include)
target_compile_definitions(nrfusion_core PUBLIC NRFUSION_CAPTURE32_STATIC=1)
if (MSVC)
    target_compile_options(nrfusion_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
else()
    target_compile_options(nrfusion_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Werror>)
endif()

if (WIN32)
    add_dependencies(nrfusion_core nrfusion_dlssnr_shader_codegen)
    target_include_directories(nrfusion_core PRIVATE "${NRFUSION_DLSSNR_GENERATED_DIR}")
endif()
