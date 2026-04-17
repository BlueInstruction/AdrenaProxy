// ============================================================
// AdrenaProxy — Frame Generation: Simple Interpolation
// Blends current and previous frames for smooth visual
// Note: Pure Extra Present mode doesn't use this shader —
// it's here for future compute-based FG improvements.
// ============================================================

SamplerState        g_sampler   : register(s0);
Texture2D<float4>   g_currColor : register(t0);
Texture2D<float4>   g_prevColor : register(t1);
RWTexture2D<float4> g_output    : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_resolution;
    float  g_t;          // Interpolation factor (0.0-1.0)
    uint   g_frameCount;
    float  g_padding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_resolution))
        return;

    float2 uv = (float2(dtid.xy) + 0.5) / g_resolution;

    float4 curr = g_currColor.SampleLevel(g_sampler, uv, 0);
    float4 prev = g_prevColor.SampleLevel(g_sampler, uv, 0);

    g_output[dtid.xy] = lerp(prev, curr, g_t);
}
