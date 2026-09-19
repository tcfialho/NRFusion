// NRFusion v0.5.4 - GPU Residual Reprojection Compute Shader
// Numerical equivalence target: nrfusion::ResidualReprojection reference implementation.

#define THREAD_GROUP_X 16
#define THREAD_GROUP_Y 16

cbuffer ResidualReprojectCB : register(b0) {
    uint g_width;
    uint g_height;
    float g_currentBlend;
    float g_minConfidence;
    float g_depthRelativeThreshold;
    float g_maxMotionPixels;
    float g_currentPreExposure;
    float g_historyPreExposure;
    uint g_cameraCut;
    uint g_historyValid;
};

Texture2D<float>  g_currResidual : register(t0);
Texture2D<float>  g_currDepth    : register(t1);
Texture2D<float>  g_histResidual : register(t2);
Texture2D<float>  g_histDepth    : register(t3);
Texture2D<float2> g_motion       : register(t4);
Texture2D<float>  g_confidence   : register(t5);

RWTexture2D<float> g_outResidual : register(u0);
RWTexture2D<uint>  g_outAccepted : register(u1);

float SampleBilinear(Texture2D<float> tex, uint w, uint h, float x, float y) {
    float xd = clamp(x, 0.0f, float(w - 1));
    float yd = clamp(y, 0.0f, float(h - 1));
    uint x0 = uint(floor(xd));
    uint y0 = uint(floor(yd));
    uint x1 = min(x0 + 1, w - 1);
    uint y1 = min(y0 + 1, h - 1);
    float fx = xd - float(x0);
    float fy = yd - float(y0);
    float a = tex.Load(int3(x0, y0, 0)) * (1.0f - fx) + tex.Load(int3(x1, y0, 0)) * fx;
    float b = tex.Load(int3(x0, y1, 0)) * (1.0f - fx) + tex.Load(int3(x1, y1, 0)) * fx;
    return a * (1.0f - fy) + b * fy;
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_width || id.y >= g_height) return;

    int3 coord = int3(id.x, id.y, 0);
    float currRes = g_currResidual.Load(coord);
    if (isnan(currRes) || isinf(currRes)) currRes = 0.0f;

    g_outResidual[id.xy] = currRes;
    g_outAccepted[id.xy] = 0;

    if (g_cameraCut != 0 || g_historyValid == 0) return;

    // Check exposure domain validity
    if (g_currentPreExposure <= 0.0f || g_historyPreExposure <= 0.0f) return;
    float exposureScale = max(1.0f, max(abs(g_currentPreExposure), abs(g_historyPreExposure)));
    if (abs(g_currentPreExposure - g_historyPreExposure) > 1.0e-5f * exposureScale) return;

    float2 m = g_motion.Load(coord);
    float rawConf = g_confidence.Load(coord);
    if (isnan(m.x) || isnan(m.y) || isinf(m.x) || isinf(m.y) ||
        isnan(rawConf) || isinf(rawConf)) return;

    if (length(m) > g_maxMotionPixels) return;
    float conf = saturate(rawConf);
    if (conf < g_minConfidence) return;

    float hx = float(id.x) - m.x;
    float hy = float(id.y) - m.y;
    if (hx < 0.0f || hy < 0.0f || hx > float(g_width - 1) || hy > float(g_height - 1)) return;

    float cd = g_currDepth.Load(coord);
    float hd = SampleBilinear(g_histDepth, g_width, g_height, hx, hy);
    if (isnan(cd) || isnan(hd) || isinf(cd) || isinf(hd)) return;

    float depthScale = max(1.0f, abs(cd));
    if (abs(cd - hd) > g_depthRelativeThreshold * depthScale) return;

    float hr = SampleBilinear(g_histResidual, g_width, g_height, hx, hy);
    if (isnan(hr) || isinf(hr)) return;

    float blend = saturate(g_currentBlend);
    float historyWeight = (1.0f - blend) * conf;
    float blended = currRes * (1.0f - historyWeight) + hr * historyWeight;
    g_outResidual[id.xy] = blended;
    g_outAccepted[id.xy] = 1;
}
