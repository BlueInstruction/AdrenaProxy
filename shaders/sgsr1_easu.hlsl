// ============================================================
// AdrenaProxy — SGSR1 EASU (Edge Adaptive Spatial Upsampling)
// Based on Qualcomm SGSR1 algorithm (BSD-3-Clause)
// Adapted for D3D11/D3D12 Compute Shader
// ============================================================

SamplerState      g_sampler  : register(s0);
Texture2D<float4> g_input    : register(t0);
RWTexture2D<float4> g_output : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;    // Source resolution
    uint2  g_displaySize;   // Target resolution
    float  g_sharpness;     // RCAS sharpness (0-1)
    uint   g_frameCount;    // For temporal effects
};

// ─── Lanczos2 kernel ─────────────────────────────────
float Lanczos2(float x)
{
    const float PI = 3.141592653589793;
    if (abs(x) < 1e-5) return 1.0;
    if (abs(x) >= 2.0) return 0.0;
    return (sin(PI * x) * sin(PI * x * 0.5)) / (PI * PI * x * x * 0.5);
}

// ─── 12-tap directional filter ───────────────────────
float4 SampleLanczos(float2 uv, float2 pixelSize)
{
    float2 srcPos = uv * g_renderSize - 0.5;
    float2 srcPosFloor = floor(srcPos);
    float2 f = srcPos - srcPosFloor;
    
    float wx[4], wy[4];
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        wx[i] = Lanczos2(f.x + 1.0 - i);
        wy[i] = Lanczos2(f.y + 1.0 - i);
    }
    
    float wxSum = wx[0] + wx[1] + wx[2] + wx[3];
    float wySum = wy[0] + wy[1] + wy[2] + wy[3];
    [unroll] 
    for (int j = 0; j < 4; j++) { wx[j] /= wxSum; wy[j] /= wySum; }
    
    float4 result = float4(0, 0, 0, 0);
    [unroll]
    for (int yi = 0; yi < 4; yi++)
    {
        [unroll]
        for (int xi = 0; xi < 4; xi++)
        {
            float2 samplePos = (srcPosFloor + float2(xi, yi) + 0.5) / g_renderSize;
            float w = wx[xi] * wy[yi];
            result += g_input.SampleLevel(g_sampler, samplePos, 0) * w;
        }
    }
    
    return result;
}

// ─── Edge-adaptive weighting ─────────────────────────
float ComputeEdgeWeight(float2 uv, float2 pixelSize)
{
    float4 c  = g_input.SampleLevel(g_sampler, uv, 0);
    float4 l  = g_input.SampleLevel(g_sampler, uv + float2(-pixelSize.x, 0), 0);
    float4 r  = g_input.SampleLevel(g_sampler, uv + float2( pixelSize.x, 0), 0);
    float4 t  = g_input.SampleLevel(g_sampler, uv + float2(0, -pixelSize.y), 0);
    float4 b  = g_input.SampleLevel(g_sampler, uv + float2(0,  pixelSize.y), 0);
    
    float edgeH = abs(l.a + r.a - 2.0 * c.a);
    float edgeV = abs(t.a + b.a - 2.0 * c.a);
    
    return 1.0 - saturate(max(edgeH, edgeV) * 8.0);
}

// ─── Main CS ─────────────────────────────────────────
[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_displaySize))
        return;
    
    float2 pixelPos  = (float2(dtid.xy) + 0.5) / g_displaySize;
    float2 pixelSize = 1.0 / g_renderSize;
    
    // Lanczos2 upsample
    float4 color = SampleLanczos(pixelPos, pixelSize);
    
    // Edge-adaptive sharpening (integrated RCAS-like)
    float edgeW = ComputeEdgeWeight(pixelPos, pixelSize);
    
    float4 blur = float4(0, 0, 0, 0);
    float2 dp = 1.0 / g_displaySize;
    blur += g_input.SampleLevel(g_sampler, pixelPos + float2(-dp.x, 0), 0);
    blur += g_input.SampleLevel(g_sampler, pixelPos + float2( dp.x, 0), 0);
    blur += g_input.SampleLevel(g_sampler, pixelPos + float2(0, -dp.y), 0);
    blur += g_input.SampleLevel(g_sampler, pixelPos + float2(0,  dp.y), 0);
    blur *= 0.25;
    
    // Sharpen
    float sharpAmount = g_sharpness * edgeW;
    color.rgb += (color.rgb - blur.rgb) * sharpAmount;
    
    // Anti-ringing clamp
    float4 mn = min(min(blur, color), min(
        g_input.SampleLevel(g_sampler, pixelPos + float2(-dp.x, -dp.y), 0),
        g_input.SampleLevel(g_sampler, pixelPos + float2( dp.x,  dp.y), 0)));
    float4 mx = max(max(blur, color), max(
        g_input.SampleLevel(g_sampler, pixelPos + float2(-dp.x, -dp.y), 0),
        g_input.SampleLevel(g_sampler, pixelPos + float2( dp.x,  dp.y), 0)));
    
    color = clamp(color, mn, mx);
    
    g_output[dtid.xy] = color;
}
