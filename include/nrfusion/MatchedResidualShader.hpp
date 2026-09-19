#pragma once

namespace nrfusion {

// Single source for the portable compute kernels used by the D3D12 provider and the
// D3D11 x86 pre-transport downsample. The consumers compile only the entry point they need.
inline constexpr const char* MatchedResidualShaderSource() noexcept {
    return R"NRFUSION(
#define THREAD_GROUP_X 16
#define THREAD_GROUP_Y 16

cbuffer DownsampleCB : register(b0) {
    uint g_srcWidth;
    uint g_srcHeight;
    uint g_dstWidth;
    uint g_dstHeight;
    float4 g_padding;
};

Texture2D<float4>   g_srcTexture : register(t0);
Texture2D<float4>   g_unusedSrv1 : register(t1);
RWTexture2D<float4> g_dstTexture : register(u0);

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CSDownsample(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstWidth || id.y >= g_dstHeight) return;

    float2 uv = (float2(id.xy) + 0.5f) / float2(g_dstWidth, g_dstHeight);
    float2 srcCoord = uv * float2(g_srcWidth, g_srcHeight) - 0.5f;

    float xd = clamp(srcCoord.x, 0.0f, float(g_srcWidth - 1));
    float yd = clamp(srcCoord.y, 0.0f, float(g_srcHeight - 1));
    uint x0 = uint(floor(xd));
    uint y0 = uint(floor(yd));
    uint x1 = min(x0 + 1, g_srcWidth - 1);
    uint y1 = min(y0 + 1, g_srcHeight - 1);
    float fx = xd - float(x0);
    float fy = yd - float(y0);

    float4 s00 = g_srcTexture.Load(int3(x0, y0, 0));
    float4 s10 = g_srcTexture.Load(int3(x1, y0, 0));
    float4 s01 = g_srcTexture.Load(int3(x0, y1, 0));
    float4 s11 = g_srcTexture.Load(int3(x1, y1, 0));

    float4 interpolated = lerp(lerp(s00, s10, fx), lerp(s01, s11, fx), fy);
    g_dstTexture[id.xy] = interpolated;
}

cbuffer ResidualExtractCB : register(b0) {
    uint g_workWidth;
    uint g_workHeight;
    float g_preExposure;
    float g_extractPadding;
    float4 g_extractPadding2;
};

Texture2D<float4>   g_neuralOutput : register(t0);
Texture2D<float4>   g_neuralInput  : register(t1);
RWTexture2D<float4> g_lowResidual  : register(u0);

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CSExtractResidual(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_workWidth || id.y >= g_workHeight) return;

    int3 coord = int3(id.xy, 0);
    float4 nOut = g_neuralOutput.Load(coord);
    float4 nIn  = g_neuralInput.Load(coord);

    if (any(isnan(nOut)) || any(isinf(nOut))) nOut = 0.0f;
    if (any(isnan(nIn))  || any(isinf(nIn)))  nIn  = 0.0f;

    g_lowResidual[id.xy] = nOut - nIn;
}

cbuffer ResidualComposeCB : register(b0) {
    uint g_nativeWidth;
    uint g_nativeHeight;
    uint g_residualWidth;
    uint g_residualHeight;
    float g_residualWeight;
    float g_composeExposure;
    float g_composePad0;
    float g_composePad1;
};

Texture2D<float4>   g_originalNative : register(t0);
Texture2D<float4>   g_residualSource : register(t1);
RWTexture2D<float4> g_composedNative : register(u0);

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CSComposeResidual(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_nativeWidth || id.y >= g_nativeHeight) return;

    int3 nativeCoord = int3(id.xy, 0);
    float4 orig = g_originalNative.Load(nativeCoord);
    if (any(isnan(orig)) || any(isinf(orig))) orig = 0.0f;

    float2 uv = (float2(id.xy) + 0.5f) / float2(g_nativeWidth, g_nativeHeight);
    float2 resCoord = uv * float2(g_residualWidth, g_residualHeight) - 0.5f;

    float rx = clamp(resCoord.x, 0.0f, float(g_residualWidth - 1));
    float ry = clamp(resCoord.y, 0.0f, float(g_residualHeight - 1));
    uint rx0 = uint(floor(rx));
    uint ry0 = uint(floor(ry));
    uint rx1 = min(rx0 + 1, g_residualWidth - 1);
    uint ry1 = min(ry0 + 1, g_residualHeight - 1);
    float rfx = rx - float(rx0);
    float rfy = ry - float(ry0);

    float4 r00 = g_residualSource.Load(int3(rx0, ry0, 0));
    float4 r10 = g_residualSource.Load(int3(rx1, ry0, 0));
    float4 r01 = g_residualSource.Load(int3(rx0, ry1, 0));
    float4 r11 = g_residualSource.Load(int3(rx1, ry1, 0));

    float4 res = lerp(lerp(r00, r10, rfx), lerp(r01, r11, rfx), rfy);
    if (any(isnan(res)) || any(isinf(res))) res = 0.0f;

    g_composedNative[id.xy] = (orig * g_composeExposure) + (res * g_residualWeight);
}

cbuffer DepthCaptureCB : register(b0) {
    uint g_depthSrcWidth;
    uint g_depthSrcHeight;
    uint g_depthDstWidth;
    uint g_depthDstHeight;
    float4 g_depthPadding;
};

Texture2D<float>    g_depthSource : register(t0);
RWTexture2D<float4> g_depthTarget : register(u0);

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CSCaptureDepth(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_depthDstWidth || id.y >= g_depthDstHeight) return;

    // Point sampling on purpose: depth is non-linear and discontinuous at silhouette edges, so
    // bilinear blending (used for color) would fabricate depth values that do not exist in the
    // scene.
    float2 uv = (float2(id.xy) + 0.5f) / float2(g_depthDstWidth, g_depthDstHeight);
    uint sx = min(uint(uv.x * g_depthSrcWidth), g_depthSrcWidth - 1);
    uint sy = min(uint(uv.y * g_depthSrcHeight), g_depthSrcHeight - 1);

    float depthValue = g_depthSource.Load(int3(sx, sy, 0));
    g_depthTarget[id.xy] = float4(depthValue, 0.0f, 0.0f, 0.0f);
}
)NRFUSION";
}

} // namespace nrfusion
