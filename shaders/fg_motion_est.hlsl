// ============================================================
// AdrenaProxy — Frame Generation: Motion Estimation
// Compute-based block matching for motion vector reconstruction
// ============================================================

SamplerState g_sampler : register(s0);

Texture2D<float4> g_currColor : register(t0);
Texture2D<float4> g_prevColor : register(t1);
Texture2D<float>  g_currDepth : register(t2);
Texture2D<float>  g_prevDepth : register(t3);
RWTexture2D<float2> g_motion  : register(u0);
RWTexture2D<float>  g_confidence : register(u1);

cbuffer Constants : register(b0)
{
    uint2  g_resolution;
    float  g_quality;        // 0.0=low, 0.5=medium, 1.0=high
    uint   g_searchRadius;   // Block matching search radius
    uint   g_frameCount;
    float2 g_padding;
};

// Block matching SAD (Sum of Absolute Differences)
float ComputeSAD(float2 center, float2 offset, float2 pixelSize)
{
    float sad = 0.0;
    const int HALF = 2; // 4x4 block
    
    [unroll]
    for (int y = -HALF; y <= HALF; y++)
    {
        [unroll]
        for (int x = -HALF; x <= HALF; x++)
        {
            float2 sampleUV = (center + float2(x, y)) * pixelSize;
            float2 prevUV   = (center + offset + float2(x, y)) * pixelSize;
            
            float3 curr = g_currColor.SampleLevel(g_sampler, sampleUV, 0).rgb;
            float3 prev = g_prevColor.SampleLevel(g_sampler, prevUV, 0).rgb;
            
            sad += dot(abs(curr - prev), float3(1, 1, 1));
        }
    }
    
    return sad;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_resolution))
        return;
    
    float2 pixelPos = float2(dtid.xy) + 0.5;
    float2 pixelSize = 1.0 / g_resolution;
    float2 uv = pixelPos * pixelSize;
    
    int searchR = (int)lerp(4, 16, g_quality);
    
    float2 bestMotion = float2(0, 0);
    float  bestSAD = 1e6;
    
    // Coarse pass (step=4)
    [loop]
    for (int dy = -searchR; dy <= searchR; dy += 4)
    {
        [loop]
        for (int dx = -searchR; dx <= searchR; dx += 4)
        {
            float2 offset = float2(dx, dy);
            float sad = ComputeSAD(pixelPos, offset, pixelSize);
            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestMotion = offset;
            }
        }
    }
    
    // Fine pass (step=1) around best coarse result
    int2 coarseInt = int2(round(bestMotion));
    [loop]
    for (int dy2 = -4; dy2 <= 4; dy2 += 1)
    {
        [loop]
        for (int dx2 = -4; dx2 <= 4; dx2 += 1)
        {
            float2 offset = float2(coarseInt) + float2(dx2, dy2);
            float sad = ComputeSAD(pixelPos, offset, pixelSize);
            if (sad < bestSAD)
            {
                bestSAD = sad;
                bestMotion = offset;
            }
        }
    }
    
    float2 motion = -bestMotion * pixelSize;
    
    // Depth-based confidence
    float currD = g_currDepth.SampleLevel(g_sampler, uv, 0);
    float prevD = g_prevDepth.SampleLevel(g_sampler, uv + motion, 0);
    float depthConfidence = 1.0 - saturate(abs(currD - prevD) * 10.0);
    float sadConfidence = 1.0 - saturate(bestSAD / 3.0);
    
    g_motion[dtid.xy] = motion;
    g_confidence[dtid.xy] = depthConfidence * sadConfidence;
}