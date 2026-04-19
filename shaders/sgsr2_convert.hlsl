// ============================================================
// AdrenaProxy — SGSR2 Convert Pass
// Based on Qualcomm SGSR v2 sgsr2_convert.comp
// BSD-3-Clause License — Copyright (c) Qualcomm Technologies, Inc.
//
// Pass 1 of 2-pass variant:
// - Dilates depth (nearest in 3x3)
// - Processes motion vectors (decode or reproject from depth)
// - Converts color to YCoCg format
// - Outputs MotionDepthClipAlphaBuffer + YCoCgColor
// ============================================================

SamplerState      g_sampler     : register(s0);
Texture2D<float4> g_inputColor  : register(t0);
Texture2D<float>  g_inputDepth  : register(t1);
Texture2D<float4> g_inputMotion : register(t2);
RWTexture2D<float4> g_motionDepthClip : register(u0);  // RGBA16F
RWTexture2D<uint4>  g_yCocgColor     : register(u1);  // R32UI

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;
    uint2  g_displaySize;
    float2 g_renderSizeRcp;
    float2 g_displaySizeRcp;
    float2 g_jitterOffset;
    float4 g_clipToPrevClip[4];    // clipToPrevClip matrix
    float  g_preExposure;
    float  g_cameraFovAngleHor;
    float  g_cameraNear;
    float  g_minLerpContribution;
    uint   g_bSameCamera;
    uint   g_reset;
};

// ─── Decode velocity from encoded texture ────────────────
float2 decodeVelocity(float2 ev)
{
    const float inv_div = 1.0f / (0.499f * 0.5f);
    return ev.xy * inv_div - (32767.0f / 65535.0f) * inv_div;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= g_renderSize))
        return;

    float2 gatherCoord = float2(DTid.xy) * g_renderSizeRcp;
    float2 viewportUV = gatherCoord + 0.5 * g_renderSizeRcp;

    // ── Depth dilation: find nearest depth in 3x3 ──
    float4 topleft     = g_inputDepth.GatherRed(g_sampler, gatherCoord);
    float2 v10         = float2(g_renderSizeRcp.x * 2.0, 0.0);
    float4 topRight    = g_inputDepth.GatherRed(g_sampler, gatherCoord + v10);
    float2 v12         = float2(0.0, g_renderSizeRcp.y * 2.0);
    float4 bottomLeft  = g_inputDepth.GatherRed(g_sampler, gatherCoord + v12);
    float2 v14         = float2(g_renderSizeRcp.x * 2.0, g_renderSizeRcp.y * 2.0);
    float4 bottomRight = g_inputDepth.GatherRed(g_sampler, gatherCoord + v14);

    // Nearest depth (minimum for reverse-Z, maximum for standard)
    float maxC = min(min(min(topleft.y, topRight.x), bottomLeft.z), bottomRight.w);
    float topleft4     = min(min(min(topleft.y, topleft.x), topleft.z), topleft.w);
    float topLeftMax9  = min(bottomLeft.w, min(min(maxC, topleft4), topRight.w));

    // ── Depth clip calculation ──
    float depthclip = 0.0;
    if (maxC < 1.0 - 1.0e-05f)
    {
        float topRight4     = min(min(min(topRight.y, topRight.x), topRight.z), topRight.w);
        float bottomLeft4   = min(min(min(bottomLeft.y, bottomLeft.x), bottomLeft.z), bottomLeft.w);
        float bottomRight4  = min(min(min(bottomRight.y, bottomRight.x), bottomRight.z), bottomRight.w);

        float Ksep = 1.37e-05f;
        float Kfov = g_cameraFovAngleHor;
        float diagonal_length = length(float2(g_renderSize));
        float Ksep_Kfov_diagonal = Ksep * Kfov * diagonal_length;

        float Depthsep = Ksep_Kfov_diagonal * (1.0 - maxC);
        float EPSILON = 1.19e-07f;

        float Wdepth = 0.0;
        Wdepth += clamp(Depthsep / (abs(maxC - topleft4 + EPSILON)), 0.0, 1.0);
        Wdepth += clamp(Depthsep / (abs(maxC - topRight4 + EPSILON)), 0.0, 1.0);
        Wdepth += clamp(Depthsep / (abs(maxC - bottomLeft4 + EPSILON)), 0.0, 1.0);
        Wdepth += clamp(Depthsep / (abs(maxC - bottomRight4 + EPSILON)), 0.0, 1.0);

        depthclip = clamp(1.0f - Wdepth * 0.25, 0.0, 1.0);
    }

    // ── Motion vector processing ──
    float4 encodedVelocity = g_inputMotion.Load(int3(DTid.xy, 0));
    float2 motion;

    if (encodedVelocity.x > 0.0)
    {
        motion = decodeVelocity(encodedVelocity.xy);
    }
    else
    {
        // Reproject from depth if no velocity available
        float2 screenPos = float2(2.0f * viewportUV - 1.0f);
        float3 position = float3(screenPos, topLeftMax9);
        float4 preClip = g_clipToPrevClip[3] +
            ((g_clipToPrevClip[2] * position.z) +
             ((g_clipToPrevClip[1] * screenPos.y) +
              (g_clipToPrevClip[0] * screenPos.x)));
        float2 preScreen = preClip.xy / preClip.w;
        motion = position.xy - preScreen;
    }

    // ── Color conversion to YCoCg ──
    float3 colorRGB = g_inputColor.Load(int3(DTid.xy, 0)).xyz;

    // Simple tonemap with pre-exposure
    float exposureRcp = g_preExposure;
    float colorMax = max(max(colorRGB.x, colorRGB.y), colorRGB.z) + exposureRcp;
    colorRGB /= float3(colorMax, colorMax, colorMax);

    // RGB → YCoCg
    float3 yCocg;
    yCocg.x = 0.25 * (colorRGB.x + 2.0 * colorRGB.y + colorRGB.z);                    // Y
    yCocg.y = clamp(0.5 * colorRGB.x + 0.5 - 0.5 * colorRGB.z, 0.0, 1.0);             // Co
    yCocg.z = clamp(yCocg.x + yCocg.y - colorRGB.x, 0.0, 1.0);                          // Cg

    // Pack YCoCg into R32UI: 11-bit Y | 11-bit Co | 10-bit Cg
    uint x11 = uint(yCocg.x * 2047.5);
    uint y11 = uint(yCocg.y * 2047.5);
    uint z10 = uint(yCocg.z * 1023.5);

    g_yCocgColor[DTid.xy] = uint4(((x11 << 21u) | (y11 << 10u)) | z10, 0, 0, 0);

    // Output motion, depthclip, and color max for upscale pass
    g_motionDepthClip[DTid.xy] = float4(motion, depthclip, colorMax);
}
