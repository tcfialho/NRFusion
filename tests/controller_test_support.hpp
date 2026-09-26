#pragma once

#include "nrfusion/AutoTuneCoordinator.hpp"
#include "nrfusion/CompatibilityDatabase.hpp"
#include "nrfusion/Diagnostics.hpp"
#include "nrfusion/ProfileStore.hpp"
#include "nrfusion/ResidualEngine.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/D3D12QueueClockBridge.hpp"
#include "nrfusion/D3D12AsyncFenceBridge.hpp"
#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/OptiScalerAdapter.hpp"
#include "nrfusion/Presets.hpp"
#include "nrfusion/TemporalConfidence.hpp"
#include "nrfusion/MgpuPlanner.hpp"
#include "nrfusion/NvofPolicy.hpp"
#include "nrfusion/TelemetryTracker.hpp"
#include "nrfusion/GuideValidation.hpp"
#include "nrfusion/MotionNormalization.hpp"
#include "nrfusion/MotionConfidence.hpp"
#include "nrfusion/TemporalHistoryRegistry.hpp"
#include "nrfusion/PipelinedExecutorState.hpp"
#include "nrfusion/Dlss5NeuralRendering.hpp"
#include "nrfusion/AdaptiveExposure.hpp"
#include "nrfusion/AdaptiveExposureController.hpp"
#include "nrfusion/QualityValidator.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace nrfusion;

static ExposureMeasurement exposureMeasurement(ExposureSource source, float whitePoint, float confidence,
                                                bool valid = true, bool sameFrame = false, FrameId frameId = 1) {
    ExposureMeasurement m;
    m.source = source;
    m.frameId = frameId;
    m.whitePoint = whitePoint;
    m.confidence = confidence;
    m.valid = valid;
    m.sameFrame = sameFrame;
    return m;
}

static TelemetrySample sample(double nr, double frame, double source, double processed, double queue, double dt = 1.0/60.0) {
    TelemetrySample s;
    s.dtSeconds = dt;
    s.nrGpuMs = nr;
    s.frameGpuMs = frame;
    s.sourceFps = source;
    s.processedFps = processed;
    s.queuePressure = queue;
    return s;
}
