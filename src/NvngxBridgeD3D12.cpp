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

  SetResource(capabilityParams, "DLSSNR.Color", color);
  SetResource(capabilityParams, "DLSSNR.Depth", depth);
  SetResource(capabilityParams, "DLSSNR.MVec", motion);
  SetResource(capabilityParams, "DLSSNR.Output", output);

  SetUInt(capabilityParams, "DLSSNR.Enabled", 1);
  SetUInt(capabilityParams, "DLSSNR.Width", width);
  SetUInt(capabilityParams, "DLSSNR.Height", height);
  SetUInt(capabilityParams, "DLSSNR.DepthInverted", static_cast<unsigned int>(depthInverted));
  SetUInt(capabilityParams, "DLSSNR.Reset", static_cast<unsigned int>(reset));
  SetUInt(capabilityParams, "DLSSNR.ColorSubrectBaseX", 0);
  SetUInt(capabilityParams, "DLSSNR.ColorSubrectBaseY", 0);
  SetUInt(capabilityParams, "DLSSNR.ColorSubrectWidth", width);
  SetUInt(capabilityParams, "DLSSNR.ColorSubrectHeight", height);
  SetUInt(capabilityParams, "DLSSNR.OutputSubrectBaseX", 0);
  SetUInt(capabilityParams, "DLSSNR.OutputSubrectBaseY", 0);
  SetUInt(capabilityParams, "DLSSNR.OutputSubrectWidth", width);
  SetUInt(capabilityParams, "DLSSNR.OutputSubrectHeight", height);
  SetUInt(capabilityParams, "DLSSNR.DepthSubrectBaseX", depthBaseX);
  SetUInt(capabilityParams, "DLSSNR.DepthSubrectBaseY", depthBaseY);
  SetUInt(capabilityParams, "DLSSNR.DepthSubrectWidth", guideWidth);
  SetUInt(capabilityParams, "DLSSNR.DepthSubrectHeight", guideHeight);
  SetUInt(capabilityParams, "DLSSNR.MVecSubrectBaseX", motionBaseX);
  SetUInt(capabilityParams, "DLSSNR.MVecSubrectBaseY", motionBaseY);
  SetUInt(capabilityParams, "DLSSNR.MVecSubrectWidth", motionWidth);
  SetUInt(capabilityParams, "DLSSNR.MVecSubrectHeight", motionHeight);
  SetFloat(capabilityParams, "DLSSNR.MVecScaleX", motionScaleX);
  SetFloat(capabilityParams, "DLSSNR.MVecScaleY", motionScaleY);

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
    float globalTone,
    ID3D12Resource* ui,
    ID3D12Resource* uiAlpha,
    ID3D12Resource* backbuffer,
    unsigned int uiWidth,
    unsigned int uiHeight,
    unsigned int backbufferWidth,
    unsigned int backbufferHeight) {
  if (!capabilityParams) return;
  (void)globalTone;
  using namespace nrfusion::bridge;
  SetResource(capabilityParams, "DLSSNR.UI", ui);
  SetResource(capabilityParams, "DLSSNR.UIAlpha", uiAlpha);
  SetResource(capabilityParams, "DLSSNR.Backbuffer", backbuffer);
  SetUInt(capabilityParams, "DLSSNR.UISubrectBaseX", 0);
  SetUInt(capabilityParams, "DLSSNR.UISubrectBaseY", 0);
  SetUInt(capabilityParams, "DLSSNR.UISubrectWidth", uiWidth);
  SetUInt(capabilityParams, "DLSSNR.UISubrectHeight", uiHeight);
  SetUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseX", 0);
  SetUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseY", 0);
  SetUInt(capabilityParams, "DLSSNR.UIAlphaSubrectWidth", uiWidth);
  SetUInt(capabilityParams, "DLSSNR.UIAlphaSubrectHeight", uiHeight);
  SetUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseX", 0);
  SetUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseY", 0);
  SetUInt(capabilityParams, "DLSSNR.BackbufferSubrectWidth", backbufferWidth);
  SetUInt(capabilityParams, "DLSSNR.BackbufferSubrectHeight", backbufferHeight);
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
