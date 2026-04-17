// ============================================================
// AdrenaProxy — Frame Generation: Frame Interpolation
// Generates intermediate frames using estimated motion
// Supports x2 (t=0.5), x3 (t=0.33/0.66), x4 (t=0.25/0.5/0.75)
// ============================================================

SamplerState        g_sampler   : register(s0);
Texture2D<float4>   g_currColor : register(t0);
Texture2D<float4>   g_prevColor : register(t1);
Texture2D<float2>   g_motion    : register(t2);
Texture2D<float>    g_confidence: register(t3);
Texture2D<float>    g_reactive  : register(t4);  // Reactive mask
RWTexture2D<float4> g_output    : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_resolution;
    float  g_t;                // Interpolation factor (0.0-1.0)
    uint   g_frameCount;
    float  g_padding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_resolution))
        return;
    
    float2 uv = (float2(dtid.xy) + 0.5) / g_resolution;
    
    // Get motion and confidence
    float2 motion = g_motion[dtid.xy];
    float  conf   = g_confidence[dtid.xy];
    float  react  = g_reactive[dtid.xy];
    
    // Bidirectional reprojection
    float2 prevUV = uv - motion * g_t;
    float2 currUV = uv + motion * (1.0 - g_t);
    
    // Sample both frames at reprojected positions
    float4 prevColor = g_prevColor.SampleLevel(g_sampler, prevUV, 0);
    float4 currColor = g_currColor.SampleLevel(g_sampler, currUV, 0);
    
    // Simple blend (no motion compensation)
    float4 simpleBlend = lerp(prevColor, currColor, g_t);
    
    // Motion-compensated blend using reprojected samples
    float4 prevWarped = g_prevColor.SampleLevel(g_sampler, prevUV, 0);
    float4 currWarped = g_currColor.SampleLevel(g_sampler, currUV, 0);
    float4 interpColor = lerp(prevWarped, currWarped, g_t);
    
    // Apply confidence weighting: high confidence uses motion-compensated, low uses simple blend
    interpColor = lerp(simpleBlend, interpColor, conf);
    
    // Reactive mask: skip interpolation for UI/particles
    float4 currentDirect = g_currColor.SampleLevel(g_sampler, uv, 0);
    interpColor = lerp(interpColor, currentDirect, react);
    
    // Boundary check
    bool prevValid = all(prevUV > 0.0) && all(prevUV < 1.0);
    bool currValid = all(currUV > 0.0) && all(currUV < 1.0);
    
    if (!prevValid || !currValid)
    {
        interpColor = lerp(prevColor, currColor, g_t);
    }
    
    g_output[dtid.xy] = interpColor;
}