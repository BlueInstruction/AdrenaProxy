// ============================================================
// AdrenaProxy — Frame Generation: Reactive Mask
// Detects disoccluded areas, UI elements, and particles
// that should not be interpolated
// ============================================================

SamplerState        g_sampler   : register(s0);
Texture2D<float4>   g_currColor : register(t0);
Texture2D<float4>   g_prevColor : register(t1);
Texture2D<float2>   g_motion    : register(t2);
Texture2D<float>    g_currDepth : register(t3);
Texture2D<float>    g_prevDepth : register(t4);
RWTexture2D<float>  g_output    : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_resolution;
    float  g_threshold;
    uint   g_frameCount;
    float  g_padding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_resolution))
        return;
    
    float2 uv = (float2(dtid.xy) + 0.5) / g_resolution;
    float2 dp = 1.0 / g_resolution;
    
    // Current and reprojected depth
    float currD = g_currDepth.SampleLevel(g_sampler, uv, 0);
    float2 motion = g_motion[dtid.xy];
    float2 prevUV = uv - motion;
    
    float prevD = 0.0;
    if (all(prevUV > 0.0) && all(prevUV < 1.0))
    {
        prevD = g_prevDepth.SampleLevel(g_sampler, prevUV, 0);
    }
    
    // Disocclusion detection
    float depthDiff = abs(currD - prevD);
    float disoccluded = saturate(depthDiff * 20.0 - 0.1);
    
    // Color variance (high variance = likely UI/particles)
    float3 currC = g_currColor.SampleLevel(g_sampler, uv, 0).rgb;
    float3 leftC = g_currColor.SampleLevel(g_sampler, uv + float2(-dp.x, 0), 0).rgb;
    float3 rightC = g_currColor.SampleLevel(g_sampler, uv + float2(dp.x, 0), 0).rgb;
    float3 topC = g_currColor.SampleLevel(g_sampler, uv + float2(0, -dp.y), 0).rgb;
    float3 bottomC = g_currColor.SampleLevel(g_sampler, uv + float2(0, dp.y), 0).rgb;
    
    float3 localVariance = (abs(currC - leftC) + abs(currC - rightC) +
                           abs(currC - topC) + abs(currC - bottomC)) * 0.25;
    float highVariance = saturate(dot(localVariance, float3(1, 1, 1)) * 5.0 - 0.3);
    
    // Alpha channel detection (common UI indicator)
    float currAlpha = g_currColor.SampleLevel(g_sampler, uv, 0).a;
    float alphaEdge = saturate((1.0 - currAlpha) * 5.0);
    
    // Combined reactive mask
    float reactive = max(max(disoccluded, highVariance), alphaEdge);
    
    g_output[dtid.xy] = reactive;
}