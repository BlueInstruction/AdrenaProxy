// ============================================================
// AdrenaProxy — SGSR1 Compute Shader
// Based on Qualcomm Snapdragon Game Super Resolution 1 (BSD-3-Clause)
// Single-pass: Lanczos2 upscaling + luma-based edge-adaptive sharpening
// Compatible with: Wine/VKD3D-Proton, Proton, Windows native (Adreno)
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

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// Lanczos2 weight — cubic approximation (no sin() for VKD3D/Turnip compat)
float Lanczos2(float x)
{
    float ax = abs(x);
    if (ax < 1e-5) return 1.0;
    if (ax >= 2.0) return 0.0;
    float ax2 = ax * ax;
    if (ax < 1.0)
        return 1.0 - 2.5 * ax2 + 1.5 * ax2 * ax;
    return -0.5 + 2.5 * ax - 4.0 * ax2 + 2.0 * ax2 * ax;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_displaySize))
        return;

    float2 uv = (float2(dtid.xy) + 0.5) / float2(g_displaySize);
    float2 rcpSrc = 1.0 / float2(g_renderSize);

    // ── Lanczos2 4x4 upsampling ──
    float2 srcPixel = uv * float2(g_renderSize) - 0.5;
    float2 srcFloor = floor(srcPixel);
    float2 f = srcPixel - srcFloor;

    float wx[4], wy[4];
    [unroll] for (int i = 0; i < 4; i++)
    {
        wx[i] = Lanczos2(f.x - (float)(i - 1));
        wy[i] = Lanczos2(f.y - (float)(i - 1));
    }

    float wxSum = wx[0] + wx[1] + wx[2] + wx[3];
    float wySum = wy[0] + wy[1] + wy[2] + wy[3];
    [unroll] for (int j = 0; j < 4; j++) { wx[j] /= wxSum; wy[j] /= wySum; }

    float4 color = float4(0, 0, 0, 0);
    [unroll] for (int sy = 0; sy < 4; sy++)
    {
        [unroll] for (int sx = 0; sx < 4; sx++)
        {
            float2 pos = (srcFloor + float2(sx - 1, sy - 1) + 0.5) * rcpSrc;
            color += g_input.SampleLevel(g_sampler, pos, 0) * (wx[sx] * wy[sy]);
        }
    }

    // ── Edge-adaptive sharpening ──
    float3 sC = g_input.SampleLevel(g_sampler, uv, 0).rgb;
    float3 sL = g_input.SampleLevel(g_sampler, uv + float2(-rcpSrc.x, 0), 0).rgb;
    float3 sR = g_input.SampleLevel(g_sampler, uv + float2( rcpSrc.x, 0), 0).rgb;
    float3 sT = g_input.SampleLevel(g_sampler, uv + float2(0, -rcpSrc.y), 0).rgb;
    float3 sB = g_input.SampleLevel(g_sampler, uv + float2(0,  rcpSrc.y), 0).rgb;

    float lumaC = Luma(sC);
    float edgeH = abs(Luma(sL) + Luma(sR) - 2.0 * lumaC);
    float edgeV = abs(Luma(sT) + Luma(sB) - 2.0 * lumaC);
    float edge = 1.0 - saturate(max(edgeH, edgeV) * 8.0);

    float3 avg = (sL + sR + sT + sB) * 0.25;
    float sharpStr = g_sharpness * edge;
    color.rgb += (color.rgb - avg) * sharpStr;

    // ── Anti-ringing clamp ──
    float3 minC = min(min(sL, sR), min(sT, sB));
    float3 maxC = max(max(sL, sR), max(sT, sB));
    color.rgb = clamp(color.rgb, minC, maxC);
    color.a = 1.0;

    g_output[dtid.xy] = color;
}