#include "NvngxBridgeD3D12.hpp"
#include "NvngxBridgeParams.hpp"
#include "NvngxBridgeSnippet.hpp"

extern "C" {

NRFUSION_BRIDGE_API int dlssnr_call_last_init = 0;
NRFUSION_BRIDGE_API int dlssnr_call_last_create = 0;

NRFUSION_BRIDGE_API const char* dlssnr_call_error() {
  return nrfusion::bridge::GetLastErrorString();
}

NRFUSION_BRIDGE_API void dlssnr_call_set_float_slot(int slot) {
  nrfusion::bridge::SetFloatSlot(slot);
}

NRFUSION_BRIDGE_API void dlssnr_call_probe_float(
    void* params, const char* name, float value, int slot) {
  nrfusion::bridge::ProbeFloat(params, name, value, slot);
}

NRFUSION_BRIDGE_API void* dlssnr_call_create(
    const wchar_t* snippetPath,
    const wchar_t* dataPath,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    void* capabilityParams,
    unsigned int width,
    unsigned int height,
    int preset,
    float intensity,
    int style,
    float localStructure,
    float localTone,
    float skinStructure,
    int useAutoMask,
    int uiCorrection) {
  using namespace nrfusion::bridge;
  std::lock_guard<std::recursive_mutex> lock(GetSnippetMutex());

  Snippet* snippet = LoadSnippetLocked(snippetPath);
  if (!snippet || !capabilityParams || !device) {
    RecordError("snippet, device or capability parameters unavailable");
    return nullptr;
  }

  if (snippet->initialisedDevices.find(device) == snippet->initialisedDevices.end() &&
      snippet->init != nullptr) {
    volatile int initResult = snippet->init(
        0x24480451ull, dataPath ? dataPath : L"", device, 0x0000015, capabilityParams);
    dlssnr_call_last_init = initResult;
    if (initResult != 1) {
      RecordError("model initialization failed", initResult);
      return nullptr;
    }
    snippet->initialisedDevices.insert(device);
  }

  SetUInt(capabilityParams, "DLSSNR.Enabled", 1);
  SetUInt(capabilityParams, "DLSSNR.Width", width);
  SetUInt(capabilityParams, "DLSSNR.Height", height);
  SetUInt(capabilityParams, "CreationNodeMask", 1);
  SetUInt(capabilityParams, "VisibilityNodeMask", 1);
  SetUInt(capabilityParams, "DLSSNR.Hint.Render.Preset", static_cast<unsigned int>(preset));
  SetFloat(capabilityParams, "DLSSNR.Intensity", intensity);
  SetUInt(capabilityParams, "DLSSNR.Style", static_cast<unsigned int>(style));
  SetFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
  SetFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
  SetFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
  SetAutomaticMask(capabilityParams, useAutoMask);
  SetUInt(capabilityParams, "DLSSNR.UICorrection", static_cast<unsigned int>(uiCorrection));

  void* handle = nullptr;
  volatile int createResult = snippet->create(cmd, 18, capabilityParams, &handle);
  dlssnr_call_last_create = createResult;
  if (createResult == 1 && handle != nullptr) {
    GetFeatureOwners()[handle] = snippet;
    return handle;
  }

  RecordError("model feature creation failed", createResult);
  return nullptr;
}

NRFUSION_BRIDGE_API int dlssnr_call_evaluate_v2(
    ID3D12GraphicsCommandList* cmd,
    void* feature,
    void* capabilityParams,
    ID3D12Resource* color,
    ID3D12Resource* depth,
    ID3D12Resource* motion,
    ID3D12Resource* output,
    unsigned int width,
    unsigned int height,
    unsigned int guideWidth,
    unsigned int guideHeight,
    unsigned int motionWidth,
    unsigned int motionHeight,
    unsigned int depthBaseX,
    unsigned int depthBaseY,
    unsigned int motionBaseX,
    unsigned int motionBaseY,
    int depthInverted,
    int reset,
    float intensity,
    int style,
    float localStructure,
    float localTone,
    float skinStructure,
    int useAutoMask,
    float motionScaleX,
    float motionScaleY) {
  if (!feature || !capabilityParams || !cmd) {
    return 0;
  }

  using namespace nrfusion::bridge;
  Snippet* snippet = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lock(GetSnippetMutex());
    auto it = GetFeatureOwners().find(feature);
    if (it == GetFeatureOwners().end()) {
      return 0;
    }
    snippet = it->second;
  }

  SetResource(capabilityParams, "DLSSNR.Feature.Create.InColour", color);
  SetResource(capabilityParams, "DLSSNR.Feature.Create.InDepth", depth);
  SetResource(capabilityParams, "DLSSNR.Feature.Create.InMotionVectors", motion);
  SetResource(capabilityParams, "DLSSNR.Feature.Create.OutColour", output);

  SetUInt(capabilityParams, "DLSSNR.Width", width);
  SetUInt(capabilityParams, "DLSSNR.Height", height);
  SetUInt(capabilityParams, "DLSSNR.GuideWidth", guideWidth);
  SetUInt(capabilityParams, "DLSSNR.GuideHeight", guideHeight);
  SetUInt(capabilityParams, "DLSSNR.MotionWidth", motionWidth);
  SetUInt(capabilityParams, "DLSSNR.MotionHeight", motionHeight);
  SetUInt(capabilityParams, "DLSSNR.DepthBaseX", depthBaseX);
  SetUInt(capabilityParams, "DLSSNR.DepthBaseY", depthBaseY);
  SetUInt(capabilityParams, "DLSSNR.MotionBaseX", motionBaseX);
  SetUInt(capabilityParams, "DLSSNR.MotionBaseY", motionBaseY);
  SetUInt(capabilityParams, "DLSSNR.InvertDepth", static_cast<unsigned int>(depthInverted));
  SetUInt(capabilityParams, "DLSSNR.Reset", static_cast<unsigned int>(reset));
  SetFloat(capabilityParams, "DLSSNR.MotionScaleX", motionScaleX);
  SetFloat(capabilityParams, "DLSSNR.MotionScaleY", motionScaleY);

  SetFloat(capabilityParams, "DLSSNR.Intensity", intensity);
  SetUInt(capabilityParams, "DLSSNR.Style", static_cast<unsigned int>(style));
  SetFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
  SetFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
  SetFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
  SetAutomaticMask(capabilityParams, useAutoMask);

  // Volatile capture forces non-tail call preserving module frame on return stack.
  volatile int result = snippet->evaluate(cmd, feature, capabilityParams, nullptr);
  if (result != 1) {
    std::lock_guard<std::recursive_mutex> lock(GetSnippetMutex());
    RecordError("model evaluation failed", result);
  }
  return result;
}

NRFUSION_BRIDGE_API void dlssnr_call_set_extras(
    void* capabilityParams,
    int denoiserMode,
    float hitDistanceRatio,
    int hitDistanceNormalization,
    float normalRoughnessWeight,
    int hairSeparation) {
  if (!capabilityParams) {
    return;
  }
  using namespace nrfusion::bridge;
  if (denoiserMode >= 0) {
    SetUInt(capabilityParams, "DLSSNR.DenoiserMode", static_cast<unsigned int>(denoiserMode));
  }
  if (hitDistanceRatio >= 0.0f) {
    SetFloat(capabilityParams, "DLSSNR.HitDistanceRatio", hitDistanceRatio);
  }
  if (hitDistanceNormalization >= 0) {
    SetUInt(capabilityParams, "DLSSNR.HitDistanceNormalization",
            static_cast<unsigned int>(hitDistanceNormalization));
  }
  if (normalRoughnessWeight >= 0.0f) {
    SetFloat(capabilityParams, "DLSSNR.NormalRoughnessWeight", normalRoughnessWeight);
  }
  if (hairSeparation >= 0) {
    SetUInt(capabilityParams, "DLSSNR.HairSeparation",
            static_cast<unsigned int>(hairSeparation));
  }
}

NRFUSION_BRIDGE_API void dlssnr_call_release(void* feature) {
  using namespace nrfusion::bridge;
  std::lock_guard<std::recursive_mutex> lock(GetSnippetMutex());
  auto it = GetFeatureOwners().find(feature);
  if (feature && it != GetFeatureOwners().end()) {
    volatile int result = it->second->release(feature);
    (void)result;
    GetFeatureOwners().erase(it);
  }
}

} // extern "C"
