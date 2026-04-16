// ============================================================
// AdrenaProxy — SGSR2 Temporal Reprojection
// TAAU-based upscaling (Experimental)
// ============================================================

SamplerState      g_sampler     : register(s0);
Texture2D<float4> g_color       : register(t0);  // Current low-res color
Texture2D<float>  g_depth       : register(t1);  // Current depth
Texture2D<float2> g_motion      : register(t2);  // Motion vectors
Texture2D<float4> g_history     : register(t3);  // Previous upscaled frame
RWTexture2D<float4> g_output    : register(u0);
RWTexture2D<float4> g_historyOut: register(u1);  // Updated history

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;
    uint2  g_displaySize;
    float  g_sharpness;
    uint   g_frameCount;
    float2 g_jitter;           // Subpixel jitter offset
    float  g_temporalWeight;   // History blend factor (0.0-1.0)
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_displaySize))
        return;
    
    float2 pixelPos = float2(dtid.xy) + 0.5;
    float2 uv = pixelPos / g_displaySize;
    
    // Upsample current color
    float2 srcUV = uv * g_renderSize / g_displaySize;
    float4 current = g_color.SampleLevel(g_sampler, srcUV, 0);
    
    // Get motion vector for this pixel
    float2 motion = g_motion.SampleLevel(g_sampler, srcUV, 0);
    
    // Reproject into history
    float2 historyUV = uv - motion;
    
    // 3x3 neighborhood variance for velocity resolve
    float2 srcPixelSize = 1.0 / g_renderSize;
    float3 colorMin = float3(1e6, 1e6, 1e6);
    float3 colorMax = float3(-1e6, -1e6, -1e6);
    float3 colorSum = float3(0, 0, 0);
    
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2(x, y) * srcPixelSize;
            float3 s = g_color.SampleLevel(g_sampler, srcUV + offset, 0).rgb;
            colorMin = min(colorMin, s);
            colorMax = max(colorMax, s);
            colorSum += s;
        }
    }
    
    // Sample history with clamp
    if (all(historyUV > 0.0) && all(historyUV < 1.0))
    {
        float4 history = g_history.SampleLevel(g_sampler, historyUV, 0);
        
        // Clamp history to neighborhood
        float3 clampedHistory = clamp(history.rgb, colorMin, colorMax);
        
        // Blend factor
        float blendFactor = g_temporalWeight;
        
        // Reduce blending for high-velocity pixels
        float velocity = length(motion) * g_displaySize.x;
        blendFactor = lerp(blendFactor, 1.0, saturate(velocity * 0.1));
        
        current.rgb = lerp(clampedHistory, current.rgb, blendFactor);
    }
    
    g_output[dtid.xy] = current;
    g_historyOut[dtid.xy] = current;
}