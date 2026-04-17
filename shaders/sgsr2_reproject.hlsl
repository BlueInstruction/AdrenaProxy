// ============================================================
// AdrenaProxy — SGSR2 Temporal Reprojection
// TAAU-based upscaling (Experimental Stub)
//
// This shader performs temporal accumulation by:
// 1. Upsampling current low-res color to display resolution
// 2. Reprojecting history from previous frame using motion vectors
// 3. Clamping history to current pixel neighborhood (anti-ghosting)
// 4. Blending current and clamped history
//
// TODO: Full temporal accumulation logic — currently passes through
// the spatial upsampling result without temporal blending.
// ============================================================

SamplerState      g_sampler     : register(s0);
Texture2D<float4> g_color       : register(t0);  // Current low-res color
Texture2D<float>  g_depth       : register(t1);  // Current depth
Texture2D<float2> g_motion      : register(t2);  // Motion vectors
Texture2D<float4> g_exposure    : register(t3);  // Exposure (optional)
Texture2D<float4> g_history     : register(t4);  // Previous upscaled frame
RWTexture2D<float4> g_output    : register(u0);  // Reprojected output
RWTexture2D<float4> g_historyOut: register(u1);  // Updated history

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;
    uint2  g_displaySize;
    float  g_sharpness;
    uint   g_frameCount;
    float2 g_jitter;           // Subpixel jitter offset
    float  g_temporalWeight;   // History blend factor (0.0-1.0)
    uint   g_resetHistory;     // 1 = reset, 0 = use history
    float  g_cameraNear;
    float  g_cameraFar;
    float  g_cameraFovY;
    float2 g_padding;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (any(dtid.xy >= g_displaySize))
        return;

    float2 pixelPos = float2(dtid.xy) + 0.5;
    float2 uv = pixelPos / g_displaySize;

    // Upsample current color using bilinear (simple for stub)
    float2 srcUV = uv * g_renderSize / g_displaySize;
    float4 current = g_color.SampleLevel(g_sampler, srcUV, 0);

    if (g_resetHistory)
    {
        // First frame — no history available
        g_output[dtid.xy] = current;
        g_historyOut[dtid.xy] = current;
        return;
    }

    // TODO: Implement full temporal reprojection:
    // 1. Get motion vector for this pixel
    // 2. Reproject into history: historyUV = uv - motion
    // 3. Compute 3x3 neighborhood variance for clamping
    // 4. Clamp history to neighborhood min/max
    // 5. Blend with velocity-adaptive weight
    // 6. Output blended result and update history

    // Stub: pass through current frame only (no temporal accumulation)
    g_output[dtid.xy] = current;
    g_historyOut[dtid.xy] = current;
}