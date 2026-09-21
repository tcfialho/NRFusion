namespace nrfusion::testing {
const char* HarnessShaderSource() noexcept {
    return R"(
cbuffer SceneBuffer : register(b0) {
    float4x4 g_currentWvp;
    float4x4 g_previousWvp;
    float4 g_jitterAndFlags;
};
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
};
struct PSInput {
    float4 positionClip : SV_POSITION;
    float4 currentNdc : POSITION_CURR;
    float4 previousNdc : POSITION_PREV;
    float3 normalWorld : NORMAL;
};
struct PSOutput {
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float reactive : SV_Target2;
};
PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos = float4(input.position, 1.0f);
    float4 currClip = mul(g_currentWvp, worldPos);
    float4 prevClip = mul(g_previousWvp, worldPos);
    output.positionClip = currClip;
    output.positionClip.x += g_jitterAndFlags.x * currClip.w;
    output.positionClip.y += g_jitterAndFlags.y * currClip.w;
    output.currentNdc = currClip;
    output.previousNdc = prevClip;
    output.normalWorld = input.normal;
    return output;
}
PSOutput PSMain(PSInput input) {
    PSOutput output;
    float3 lightDir = normalize(float3(0.577f, 0.577f, -0.577f));
    float diff = max(dot(normalize(input.normalWorld), lightDir), 0.15f);
    output.color = float4(diff * 1.8f, diff * 1.2f, diff * 0.9f, 1.0f);
    float2 currNdc = input.currentNdc.xy / input.currentNdc.w;
    float2 prevNdc = input.previousNdc.xy / input.previousNdc.w;
    float2 uvDiff = (prevNdc - currNdc) * float2(0.5f, -0.5f);
    if (g_jitterAndFlags.z > 0.5f) {
        uvDiff = float2(0.0f, 0.0f);
    }
    output.motion = uvDiff;
    output.reactive = 0.2f;
    return output;
}
RWStructuredBuffer<float> g_exposureOutput : register(u0);
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    float acc = 0.0f;
    [loop]
    for (int i = 0; i < 200; ++i) {
        acc += sin(float(i) + float(id.x)) * cos(float(id.y));
    }
    if (id.x == 0 && id.y == 0) {
        g_exposureOutput[0] = acc;
    }
}
)";
}
}
