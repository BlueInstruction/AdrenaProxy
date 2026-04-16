// ============================================================
// AdrenaProxy — SGSR1 RCAS (Robust Contrast Adaptive Sharpening)
// Separate sharpening pass for use after EASU
// ============================================================

SamplerState      g_sampler  : register(s0);
Texture2D<float4> g_input    : register(t0);
RWTexture2D<float4> g_output : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;
    uint2  g_displaySize;
    float  g_sharpness;
    uint   g_frameCount;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_displaySize))
        return;
    
    float2 uv = (float2(dtid.xy) + 0.5) / g_displaySize;
    float2 dp = 1.0 / g_displaySize;
    
    // 5-tap cross kernel
    float4 c = g_input.SampleLevel(g_sampler, uv, 0);
    float4 l = g_input.SampleLevel(g_sampler, uv + float2(-dp.x, 0), 0);
    float4 r = g_input.SampleLevel(g_sampler, uv + float2( dp.x, 0), 0);
    float4 t = g_input.SampleLevel(g_sampler, uv + float2(0, -dp.y), 0);
    float4 b = g_input.SampleLevel(g_sampler, uv + float2(0,  dp.y), 0);
    
    // Min/Max for anti-ringing
    float4 mn = min(min(l, r), min(t, b));
    float4 mx = max(max(l, r), max(t, b));
    
    // Contrast-adaptive sharpening weight
    float peak = -1.0 / max(0.001, lerp(1.0, 3.0, 1.0 - g_sharpness));
    
    float4 w = float4(1, 1, 1, 1) + float4(peak, peak, peak, peak);
    float4 weight = float4(1, 1, 1, 1) + 4.0 * w;
    
    float4 result = (c + (l + r + t + b) * w) / weight;
    
    // Anti-ringing clamp
    result = clamp(result, mn, mx);
    
    g_output[dtid.xy] = result;
}