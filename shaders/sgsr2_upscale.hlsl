// ============================================================
// AdrenaProxy — SGSR2 Upscale Pass
// Based on Qualcomm SGSR v2 sgsr2_upscale.comp
// BSD-3-Clause License — Copyright (c) Qualcomm Technologies, Inc.
//
// Pass 2 of 2-pass variant:
// - Lanczos-like upsampling from YCoCg color
// - Variance-based neighborhood clamping
// - Temporal accumulation with history
// ============================================================

SamplerState      g_sampler          : register(s0);
Texture2D<float4> g_prevHistory      : register(t1);
Texture2D<float4> g_motionDepthClip  : register(t2);
Texture2D<uint4>  g_yCocgColor       : register(t3);
RWTexture2D<float4> g_output         : register(u0);  // SceneColorOutput
RWTexture2D<float4> g_historyOut     : register(u1);  // HistoryOutput

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;
    uint2  g_displaySize;
    float2 g_renderSizeRcp;
    float2 g_displaySizeRcp;
    float2 g_jitterOffset;
    float4 g_clipToPrevClip[4];
    float  g_preExposure;
    float  g_cameraFovAngleHor;
    float  g_cameraNear;
    float  g_minLerpContribution;
    uint   g_sameCameraFrmNum;
    uint   g_reset;
};

// ─── FastLanczos ─────────────────────────────────────────
float FastLanczos(float base)
{
    float y = base - 1.0f;
    float y2 = y * y;
    float y_temp = 0.75f * y + y2;
    return y_temp * y2;
}

// ─── Decode YCoCg from packed R32UI ─────────────────────
float3 DecodeColor(uint sample32)
{
    uint x11 = sample32 >> 21u;
    uint y11 = sample32 & (2047u << 10u);
    uint z10 = sample32 & 1023u;

    float3 c;
    c.x = float(x11) * (1.0 / 2047.5);
    c.y = float(y11) * (4.76953602e-7) - 0.5;
    c.z = float(z10) * (1.0 / 1023.5) - 0.5;
    return c;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= g_displaySize))
        return;

    float biasmax_viewportXScale = min(float(g_displaySize.x) / float(g_renderSize.x), 1.99);
    float scalefactor = min(20.0, pow((float(g_displaySize.x) / float(g_renderSize.x)) *
                                       (float(g_displaySize.y) / float(g_renderSize.y)), 3.0));
    float f2 = g_preExposure;
    float2 HistoryInfoViewportSizeInverse = g_displaySizeRcp;
    float2 HistoryInfoViewportSize = float2(g_displaySize);
    float2 InputJitter = g_jitterOffset;
    float2 InputInfoViewportSize = float2(g_renderSize);

    // Output UV in display space
    float2 Hruv = (float2(DTid.xy) + 0.5) * HistoryInfoViewportSizeInverse;

    // Jittered UV for source sampling
    float2 Jitteruv;
    Jitteruv.x = clamp(Hruv.x + (InputJitter.x * g_renderSizeRcp.x), 0.0, 1.0);
    Jitteruv.y = clamp(Hruv.y + (InputJitter.y * g_renderSizeRcp.y), 0.0, 1.0);

    int2 InputPos = int2(Jitteruv * InputInfoViewportSize);

    // Read motion/depthclip from convert pass
    float4 mda = g_motionDepthClip.SampleLevel(g_sampler, Jitteruv, 0.0);
    float2 Motion = mda.xy;

    // Reproject into history
    float2 PrevUV;
    PrevUV.x = clamp(-0.5 * Motion.x + Hruv.x, 0.0, 1.0);
    PrevUV.y = clamp(-0.5 * Motion.y + Hruv.y, 0.0, 1.0);

    float depthfactor = mda.z;
    float ColorMax = mda.w;

    // Sample history at reprojected position
    float4 History = g_prevHistory.SampleLevel(g_sampler, PrevUV, 0.0);
    float3 HistoryColor = History.xyz;
    float Historyw = History.w;
    float Wfactor = clamp(abs(Historyw), 0.0, 1.0);

    // ── Upsample and compute variance box ──
    float4 Upsampledcw = float4(0.0, 0.0, 0.0, 0.0);
    float kernelfactor = clamp(Wfactor + float(g_reset), 0.0, 1.0);
    float biasmax = biasmax_viewportXScale - biasmax_viewportXScale * kernelfactor;
    float biasmin = max(1.0f, 0.3 + 0.3 * biasmax);
    float biasfactor = max(0.25f, kernelfactor);
    float kernelbias = lerp(biasmax, biasmin, biasfactor);
    float motion_viewport_len = length(Motion * HistoryInfoViewportSize);
    float curvebias = lerp(-2.0, -3.0, clamp(motion_viewport_len * 0.02, 0.0, 1.0));

    float3 rectboxcenter = float3(0.0, 0.0, 0.0);
    float3 rectboxvar = float3(0.0, 0.0, 0.0);
    float rectboxweight = 0.0;
    float2 srcpos = float2(InputPos) + 0.5 - InputJitter;
    float2 srcOutputPos = Hruv * InputInfoViewportSize;

    kernelbias *= 0.5f;
    float kernelbias2 = kernelbias * kernelbias;
    float2 srcpos_srcOutputPos = srcpos - srcOutputPos;

    int2 InputPosBtmRight = int2(1, 1) + InputPos;
    float2 gatherCoord = float2(InputPos) * g_renderSizeRcp;

    uint btmRight = g_yCocgColor.Load(int3(InputPosBtmRight, 0)).x;
    uint4 topleft = g_yCocgColor.GatherRed(g_sampler, gatherCoord);
    uint2 topRight;
    uint2 bottomLeft;

    if (g_sameCameraFrmNum != 0u)
    {
        topRight = g_yCocgColor.GatherRed(g_sampler, gatherCoord + float2(g_renderSizeRcp.x, 0.0)).yz;
        bottomLeft = g_yCocgColor.GatherRed(g_sampler, gatherCoord + float2(0.0, g_renderSizeRcp.y)).xy;
    }
    else
    {
        uint2 br = g_yCocgColor.GatherRed(g_sampler, gatherCoord + float2(g_renderSizeRcp.x, g_renderSizeRcp.y)).xz;
        bottomLeft.y = br.x;
        topRight.x = br.y;
    }

    // ── 9-tap Lanczos upsample + box computation ──
    float3 rectboxmin;
    float3 rectboxmax;

    // bottomLeft.y
    {
        float3 samplecolor = DecodeColor(bottomLeft.y);
        float2 baseoffset = srcpos_srcOutputPos + float2(0.0, 1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = samplecolor;
        rectboxmax = samplecolor;
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topRight.x
    {
        float3 samplecolor = DecodeColor(topRight.x);
        float2 baseoffset = srcpos_srcOutputPos + float2(1.0, 0.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topleft.x (left)
    {
        float3 samplecolor = DecodeColor(topleft.x);
        float2 baseoffset = srcpos_srcOutputPos + float2(-1.0, 0.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topleft.y (center)
    {
        float3 samplecolor = DecodeColor(topleft.y);
        float2 baseoffset = srcpos_srcOutputPos;
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topleft.z (top)
    {
        float3 samplecolor = DecodeColor(topleft.z);
        float2 baseoffset = srcpos_srcOutputPos + float2(0.0, -1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topleft.w
    {
        float3 samplecolor = DecodeColor(topleft.w);
        float2 baseoffset = srcpos_srcOutputPos + float2(1.0, -1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // btmRight
    {
        float3 samplecolor = DecodeColor(btmRight);
        float2 baseoffset = srcpos_srcOutputPos + float2(1.0, 1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // bottomLeft.x
    {
        float3 samplecolor = DecodeColor(bottomLeft.x);
        float2 baseoffset = srcpos_srcOutputPos + float2(-1.0, 1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }
    // topRight.y
    {
        float3 samplecolor = DecodeColor(topRight.y);
        float2 baseoffset = srcpos_srcOutputPos + float2(-1.0, -1.0);
        float baseoffset_dot = dot(baseoffset, baseoffset);
        float base = clamp(baseoffset_dot * kernelbias2, 0.0f, 1.0f);
        float weight = FastLanczos(base);
        Upsampledcw += float4(samplecolor * weight, weight);
        float boxweight = exp(baseoffset_dot * curvebias);
        rectboxmin = min(rectboxmin, samplecolor);
        rectboxmax = max(rectboxmax, samplecolor);
        float3 wsample = samplecolor * boxweight;
        rectboxcenter += wsample;
        rectboxvar += (samplecolor * wsample);
        rectboxweight += boxweight;
    }

    // ── Compute upsampled color ──
    float3 Upsampled = Upsampledcw.xyz / Upsampledcw.w;

    // ── Variance-based clamping of history ──
    float3 boxCenter = rectboxcenter / rectboxweight;
    float3 boxVar = abs((rectboxvar / rectboxweight) - (boxCenter * boxCenter));
    float3 boxSigma = sqrt(boxVar) * 2.0;
    float3 clampMin = rectboxmin - boxSigma;
    float3 clampMax = rectboxmax + boxSigma;

    // Clamp history to variance box
    float3 clampedHistory = clamp(HistoryColor, clampMin, clampMax);

    // Blend between original and clamped history
    float lerpFactor = g_minLerpContribution;
    if (g_reset != 0u) lerpFactor = 1.0;
    float3 blendedHistory = lerp(HistoryColor, clampedHistory, lerpFactor);

    // Final blend: upsampled vs history
    float historyWeight = kernelfactor;
    float3 finalColor = lerp(Upsampled, blendedHistory, historyWeight);

    // ── YCoCg → RGB ──
    float Y  = finalColor.x;
    float Co = finalColor.y;
    float Cg = finalColor.z;

    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;

    // Apply color max (undo tonemap)
    float3 outRGB = float3(R, G, B) * ColorMax;

    // Write outputs
    g_output[DTid.xy] = float4(outRGB, 1.0);

    // History output for next frame (store YCoCg + weight)
    g_historyOut[DTid.xy] = float4(finalColor, kernelfactor);
}
