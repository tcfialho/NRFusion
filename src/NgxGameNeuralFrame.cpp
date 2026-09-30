#include "NgxGameNeuralHook.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "NgxGameProxyDiagnostics.hpp"
#include <algorithm>

namespace nrfusion {
namespace {

unsigned int ReadUnsigned(NgxParameter* parameters, const char* name, unsigned int fallback = 0) {
    unsigned int value = fallback;
    if (parameters->Get(name, &value) == 1) return value;
    int signedValue = static_cast<int>(fallback);
    return parameters->Get(name, &signedValue) == 1 ? static_cast<unsigned int>(signedValue) : fallback;
}

float ReadFloat(NgxParameter* parameters, const char* name, float fallback) {
    float value = fallback;
    return parameters->Get(name, &value) == 1 ? value : fallback;
}

ID3D12Resource* ReadResource(NgxParameter* parameters, const char* name) {
    ID3D12Resource* resource = nullptr;
    return parameters->Get(name, &resource) == 1 ? resource : nullptr;
}

D3D12NrSubrect GuideRect(NgxParameter* parameters, const D3D12_RESOURCE_DESC& desc,
                         const char* xKey, const char* yKey, UINT width, UINT height) {
    const UINT x = ReadUnsigned(parameters, xKey);
    const UINT y = ReadUnsigned(parameters, yKey);
    const UINT availableWidth = x < desc.Width ? static_cast<UINT>(desc.Width) - x : 0;
    const UINT availableHeight = y < desc.Height ? desc.Height - y : 0;
    return {x, y, std::min(width, availableWidth), std::min(height, availableHeight)};
}

} // namespace

D3D12NrFrameResult ExecuteGameNeuralFrame(D3D12NrExecutor& executor,
    ID3D12GraphicsCommandList* commands, NgxParameter* parameters,
    const GameNeuralFrameContext& context) {
    if (!commands || !parameters) return D3D12NrFrameResult::Failed;
    D3D12NrFrameResources resources{};
    resources.color = ReadResource(parameters, "Color");
    resources.depth = ReadResource(parameters, "Depth");
    resources.motion = ReadResource(parameters, "MotionVectors");
    resources.output = ReadResource(parameters, "Output");
    if (!resources.output || !resources.depth || !resources.motion) return D3D12NrFrameResult::Failed;
    const auto output = resources.output->GetDesc();
    const auto depth = resources.depth->GetDesc();
    const auto motion = resources.motion->GetDesc();
    const auto color = resources.color ? resources.color->GetDesc() : D3D12_RESOURCE_DESC{};
    const UINT renderWidth = ReadUnsigned(parameters, "DLSS.Render.Subrect.Dimensions.Width", static_cast<UINT>(depth.Width));
    const UINT renderHeight = ReadUnsigned(parameters, "DLSS.Render.Subrect.Dimensions.Height", depth.Height);
    D3D12NrFrameRequest request{};
    request.plan.activeColor = {0, 0, static_cast<UINT>(output.Width), output.Height};
    if (context.beforeUpscale) {
        if (!resources.color) return D3D12NrFrameResult::Failed;
        request.plan.activeColor = GuideRect(parameters, color, "DLSS.Input.Color.Subrect.Base.X",
                                           "DLSS.Input.Color.Subrect.Base.Y", renderWidth, renderHeight);
    }
    request.plan.depth = GuideRect(parameters, depth, "DLSS.Input.Depth.Subrect.Base.X",
                                  "DLSS.Input.Depth.Subrect.Base.Y", renderWidth, renderHeight);
    request.plan.motion = GuideRect(parameters, motion, "DLSS.Input.MV.Subrect.Base.X",
        "DLSS.Input.MV.Subrect.Base.Y", (context.createFlags & 2u) ? renderWidth : static_cast<UINT>(motion.Width),
        (context.createFlags & 2u) ? renderHeight : motion.Height);
    request.plan.execution.workingScale = context.workingScale;
    request.plan.execution.proxyBackend = false;
    request.plan.execution.passes = context.advanced.nr.multipassEnabled ? context.advanced.nr.passCount : 1;
    request.plan.beforeUpscale = context.beforeUpscale;
    request.composition.runBeforeUpscale = context.runBeforeUpscale;
    request.composition.rayReconstruction = context.rayReconstruction;
    request.composition.residualAcrossRr = context.rayReconstruction && context.runBeforeUpscale &&
                                          context.advanced.nr.residualEnabled;
    request.composition.residualBlend = context.advanced.nr.residualBlend;
    request.composition.colourIsLinearHdr = (context.createFlags & 1u) != 0;
    request.submissionEpoch = context.epoch;
    request.reset = ReadUnsigned(parameters, "Reset") != 0;
    request.depthInverted = (context.createFlags & (1u << 3)) != 0;
    request.motionScaleX = ReadFloat(parameters, "MV.Scale.X", 1.0f);
    request.motionScaleY = ReadFloat(parameters, "MV.Scale.Y", 1.0f);
    const auto& appearance = context.advanced.nr.appearance;
    for (UINT pass = 0; pass < request.plan.execution.passes; ++pass) {
        request.tuning[pass].intensity = appearance.intensity;
        request.tuning[pass].style = static_cast<int>(appearance.style);
        request.tuning[pass].localStructure = appearance.localStructure;
        request.tuning[pass].skinStructure = appearance.skinStructure.value_or(-1.0f);
        request.tuning[pass].autoMask = appearance.automaticMask != TriState::Off;
    }
    ngxproxy::BeginDiagnosticPass(commands);
    const auto result = executor.ExecuteFrame(commands, resources, request);
    ngxproxy::EndDiagnosticPass(commands, result == D3D12NrFrameResult::Applied);
    return result;
}

} // namespace nrfusion
