// ResidualAcrossRR v2 -- the MV-reprojected temporal accumulator for the pre-SR NR residual.
//
// Deliberately a SEPARATE shader from dlssnr.hlsl. Regenerating dlssnr.hlsl's blob with a current
// dxc produces materially different DXIL from the committed one (older compiler), and that shader
// carries every NR path -- post-SR, pre-SR, RR, DeferredDLSS, ResidualFG. These two experimental
// modes get their own tiny blob and a second compute PSO instead, so the battle-tested one is
// never touched. The cbuffer and bindings mirror dlssnr.hlsl exactly so DlssNr_Dx12's existing
// root signature and descriptor table are reused as-is; only gResidualBlend is appended, and it
// fits inside DlssNrConstants' existing 256-byte alignment with no size change.
//
//   gMode == 0  Accumulate: (edited - original) blended into the MV-reprojected history layer.
//               history_t = lerp( reproject(history_{t-1}), edited - original, blend )
//               The per-frame ray-trace noise term of (edited - original) is temporally
//               uncorrelated and averages to zero; the enhancement term follows geometry and
//               persists. Invalid reprojection (off-screen / bad MV) -> the
//               history is treated as zero at that pixel and rebuilds over the next frames.
//   gMode == 1  Apply: base + delta * gTransferStrength, clamped non-negative. Run after RR+SR
//               with the upscaled history layer as the delta.

#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    uint  gMode;
    float gWhitePoint;
    uint  gWidth;
    uint  gHeight;
    float gTransferStrength;
    float gColourStrength;
    uint  gDebugView;
    float gMaxRatio;
    uint  gPassthrough;
    float gMvScaleX;
    float gMvScaleY;
    uint  gGuideWidth;
    uint  gGuideHeight;
    uint  gCompareMode;
    float gCompareSplit;
    float gCompareZoom;
    uint  gCompareSwap;
    uint  gTransfer;
    float gDebugScale;
    uint  gReversibleMode;
    uint  gApplyModel;
    uint  gUseGameExposure;
    float gExposurePreMul;
    uint  gSkinProtection;
    uint  gShowSkinMask;
    float gSkinDetail;
    float gSkinColour;
    float gEnvironmentDetail;
    float gEnvironmentColour;
    float gResidualBlend;   // v2 only: history blend rate, 0..1. 1 == no accumulation (== v1).
    uint gResidualHistoryValid;
    uint gResidualMotionBaseX;
    uint gResidualMotionBaseY;
};

// Same registers and the same SPIR-V binding numbers as dlssnr.hlsl, including the slots these
// modes do not read (gExposure t4, gKeep u1) -- DispatchResidualPass binds a stand-in into them
// exactly as DispatchPass does, and a future Vulkan host path needs the numbering to line up.
#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4>   gSource   : register(t0);  // accumulate: the untouched pre-SR frame. apply: the RR+SR output.
#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float4>   gModel    : register(t1);  // accumulate: the NR-edited frame. apply: the upscaled delta layer.
#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float4>   gOriginal : register(t2);  // accumulate: the previous history layer.
#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
Texture2D<float4>   gMotion   : register(t3);  // raw game motion; active size, offsets and scale come from the host.
#ifndef VK_MODE
Texture2D<float4>   gExposure : register(t4);  // unused here; bound for descriptor-table parity.
#endif
#ifdef VK_MODE
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> gTarget   : register(u0);  // accumulate: the new history layer. apply: the composed frame.
#ifdef VK_MODE
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> gKeep     : register(u1);  // unused here; bound for descriptor-table parity.
#ifdef VK_MODE
[[vk::binding(7, 0)]]
#endif
SamplerState        gLinear   : register(s0);  // history is sampled at the reprojected coordinate.

float  SanitizeFinite(float v, float fallback)   { return isfinite(v) ? v : fallback; }
float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float3 CbrtSigned(float3 v) { return sign(v) * pow(abs(v), 1.0 / 3.0); }

float3 ToOkLab(float3 color)
{
    const float3x3 rgb_to_lms = { 0.4122214708, 0.5363325363, 0.0514459929,
                                  0.2119034982, 0.6806995451, 0.1073969566,
                                  0.0883024619, 0.2817188376, 0.6299787005 };
    const float3x3 lms_to_lab = { 0.2104542553, 0.7936177850, -0.0040720468,
                                  1.9779984951, -2.4285922050, 0.4505937099,
                                  0.0259040371, 0.7827717662, -0.8086757660 };
    return mul(lms_to_lab, CbrtSigned(mul(rgb_to_lms, color)));
}

float3 FromOkLab(float3 lab)
{
    const float3x3 lab_to_lms = { 1.0, 0.3963377774, 0.2158037573,
                                  1.0, -0.1055613458, -0.0638541728,
                                  1.0, -0.0894841775, -1.2914855480 };
    const float3x3 lms_to_rgb = { 4.0767416621, -3.3077115913, 0.2309699292,
                                  -1.2684380046, 2.6097574011, -0.3413193965,
                                  -0.0041960863, -0.7034186147, 1.7076147010 };
    float3 lms = mul(lab_to_lms, lab);
    return mul(lms_to_rgb, lms * lms * lms);
}

float3 BoundResidualColor(float3 c, float3 ref, float maxRatio, float colourStrength)
{
    c = max(c, 0.0);
    ref = max(ref, 0.0);
    float cLuma = dot(c, kLuma);
    float refLuma = dot(ref, kLuma);
    const float kRatioFloor = 1.0 / 512.0;
    float ratio = (cLuma + kRatioFloor) / (refLuma + kRatioFloor);
    float guard = max(maxRatio, 1.0);
    float boundedRatio = clamp(ratio, 1.0 / guard, guard);
    float3 cScaled = c * (boundedRatio / max(ratio, 1e-6));

    float3 labRef = ToOkLab(ref);
    float3 labC = ToOkLab(cScaled);
    float2 dChroma = labC.yz - labRef.yz;
    float dist = length(dChroma);
    float maxDist = max(labRef.x * 0.20 * saturate(colourStrength), 0.01 * saturate(colourStrength));
    if (dist > maxDist && dist > 1e-6)
    {
        dChroma *= maxDist / dist;
        labC.yz = labRef.yz + dChroma;
        cScaled = FromOkLab(labC);
    }
    return max(cScaled, 0.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    if (gMode == 0)
    {
        float3 source = SanitizeFinite3(gSource.Load(int3(id.xy, 0)).rgb, float3(0.0, 0.0, 0.0));
        float3 model  = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb, float3(0.0, 0.0, 0.0));

        float3 boundedModel = BoundResidualColor(model, source, gMaxRatio, gColourStrength);
        float3 rawDelta = model - source;
        float3 boundedDelta = boundedModel - source;
        float3 delta = (gDebugView == 1) ? rawDelta : boundedDelta;

        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        uint2 guideSize = uint2(gGuideWidth, gGuideHeight);
        uint2 guidePos = min(uint2(uv * guideSize), guideSize - 1) +
                         uint2(gResidualMotionBaseX, gResidualMotionBaseY);
        float2 motion = gMotion.Load(int3(guidePos, 0)).xy * float2(gMvScaleX, gMvScaleY);
        float2 prevUV = uv + motion;

        bool valid = gResidualHistoryValid != 0 && all(isfinite(motion)) && all(abs(motion) < 2.0) &&
                     all(prevUV >= 0.0) && all(prevUV <= 1.0);

        float3 history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : float3(0.0, 0.0, 0.0);
        history = SanitizeFinite3(history, float3(0.0, 0.0, 0.0));

        // Invalid reprojection: history is 0, so the pixel fades in from no edit at the normal blend
        // rate over the next frames. A cold start/cut also fades in, without sampling uninitialized history.
        float a = clamp(gResidualBlend, 0.0, 1.0);

        gTarget[id.xy] = float4(lerp(history, delta, a), 1.0);
        return;
    }

    if (gMode == 1)
    {
        float4 base  = gSource.Load(int3(id.xy, 0));
        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        float3 delta = SanitizeFinite3(gModel.SampleLevel(gLinear, uv, 0).rgb, float3(0.0, 0.0, 0.0));

        if (gDebugView == 1 || gDebugView == 2 || gDebugView == 3)
        {
            float3 shown = saturate(0.5 + delta * 10.0);
            gTarget[id.xy] = float4(shown, base.a);
            return;
        }

        float guard = max(gMaxRatio, 1.0);
        float3 rawComposed = base.rgb + delta * gTransferStrength;
        float3 nonNegativeComposed = max(rawComposed, base.rgb / guard);
        float3 composed = BoundResidualColor(nonNegativeComposed, base.rgb, guard, gColourStrength);

        gTarget[id.xy] = float4(max(composed, 0.0), base.a);
        return;
    }

    gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
}
