//============================================================================
//  SGSR1 Official — Compute Shader for AdrenaProxy
//  Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
//  SPDX-License-Identifier: BSD-3-Clause
//
//  Converted from official SGSR1 pixel shader to compute shader.
//  Supports: RGBA (mode 1), RGBY (mode 3), LERP (mode 4)
//  Optional edge direction for improved quality.
//============================================================================

#define SGSR_MOBILE

cbuffer SGSR1RootConstants : register(b0)
{
    uint  g_renderW;          // offset  0
    uint  g_renderH;          // offset  4
    uint  g_displayW;         // offset  8
    uint  g_displayH;         // offset 12
    float g_sharpness;        // offset 16  (EdgeSharpness, default 2.0)
    uint  g_frameCount;       // offset 20
    float g_edgeThreshold;    // offset 24  (8/255 mobile, 4/255 VR)
    uint  g_operationMode;    // offset 28  (1=RGBA, 3=RGBY, 4=LERP)
    uint  g_useEdgeDir;       // offset 32  (0=off, 1=on)
    float g_padding0;         // offset 36
    float g_padding1;         // offset 40
};

Texture2D<half4>     InputTexture   : register(t0);
RWTexture2D<half4>   OutputTexture  : register(u0);
SamplerState         samLinearClamp : register(s0);

// ── Gather helpers ──

half4 SGSRRH(float2 p)
{
    return InputTexture.GatherRed(samLinearClamp, p);
}

half4 SGSRGH(float2 p)
{
    return InputTexture.GatherGreen(samLinearClamp, p);
}

half4 SGSRBH(float2 p)
{
    return InputTexture.GatherBlue(samLinearClamp, p);
}

half4 SGSRAH(float2 p)
{
    return InputTexture.GatherAlpha(samLinearClamp, p);
}

half4 SGSRRGBH(float2 p)
{
    return InputTexture.SampleLevel(samLinearClamp, p, 0);
}

half4 SGSRH(float2 p, uint channel)
{
    if (channel == 0) return SGSRRH(p);
    if (channel == 1) return SGSRGH(p);
    if (channel == 2) return SGSRBH(p);
    return SGSRAH(p);
}

// ── Fast Lanczos-2 kernel ──

half fastLanczos2(half x)
{
    half wA = x - half(4.0);
    half wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

// ── Weight function (no edge direction) ──

half2 weightY(half dx, half dy, half c, half data)
{
    half std = data;
    half x = ((dx * dx) + (dy * dy)) * half(0.5)
           + clamp(abs(c) * std, 0.0, 1.0);
    half w = fastLanczos2(x);
    return half2(w, w * c);
}

// ── Weight function (with edge direction) ──

half2 weightY_edgeDir(half dx, half dy, half c, half3 data)
{
    half std = data.x;
    half2 dir = data.yz;

    half edgeDis = (dx * dir.y) + (dy * dir.x);
    half x = ((dx * dx) + (dy * dy))
           + ((edgeDis * edgeDis) *
              ((clamp((c * c) * std, 0.0, 1.0) * half(0.7)) - half(1.0)));

    half w = fastLanczos2(x);
    return half2(w, w * c);
}

// ── Edge direction estimation ──

half2 edgeDirection(half4 left, half4 right)
{
    half RxLz = right.x + (-left.z);
    half RwLy = right.w + (-left.y);

    half2 delta;
    delta.x = RxLz + RwLy;
    delta.y = RxLz + (-RwLy);

    half lengthInv = rsqrt(
        delta.x * delta.x + half(3.075740e-05) + delta.y * delta.y);

    return half2(delta.x * lengthInv, delta.y * lengthInv);
}

// ── Main SGSR1 algorithm ──

void SgsrYuvH(out half4 pix, float2 uv, float4 con1)
{
    int  mode          = (int)g_operationMode;
    half edgeThreshold = (half)g_edgeThreshold;
    half edgeSharpness = (half)g_sharpness;

    // Sample center pixel
    if (mode == 1)
        pix.xyz = SGSRRGBH(uv).xyz;
    else
        pix.xyzw = SGSRRGBH(uv).xyzw;

    // Upscaling region (mode 4 = LERP = simple, no edge processing)
    if (mode != 4)
    {
        float2 imgCoord      = (uv * con1.zw) + float2(-0.5, 0.5);
        float2 imgCoordPixel = floor(imgCoord);
        float2 coord         = imgCoordPixel * con1.xy;
        half2  pl            = (half2)(imgCoord + (-imgCoordPixel));

        // Gather left 2×2 block
        half4 left = SGSRH(coord, (uint)mode);

        // Edge vote
        half edgeVote = abs(left.z - left.y)
                      + abs(pix[mode] - left.y)
                      + abs(pix[mode] - left.z);

        if (edgeVote > edgeThreshold)
        {
            coord.x += con1.x;

            // Gather right 2×2 block
            half4 right = SGSRH(
                coord + float2(con1.x, 0.0), (uint)mode);

            // Gather up/down pixels
            half4 upDown;
            upDown.xy = SGSRH(
                coord + float2(0.0, -con1.y), (uint)mode).wz;
            upDown.zw = SGSRH(
                coord + float2(0.0,  con1.y), (uint)mode).yx;

            // Subtract mean
            half mean = (left.y + left.z + right.x + right.w) * half(0.25);
            left   = left   - half4(mean, mean, mean, mean);
            right  = right  - half4(mean, mean, mean, mean);
            upDown = upDown - half4(mean, mean, mean, mean);
            pix.w  = pix[mode] - mean;

            // Compute standard deviation
            half sum = abs(left.x) + abs(left.y)
                     + abs(left.z) + abs(left.w)
                     + abs(right.x) + abs(right.y)
                     + abs(right.z) + abs(right.w)
                     + abs(upDown.x) + abs(upDown.y)
                     + abs(upDown.z) + abs(upDown.w);

            half sumMean = half(1.014185e+01) / sum;
            half std = sumMean * sumMean;

            // Accumulate 12-tap weights
            half2 aWY = half2(0.0, 0.0);

            if (g_useEdgeDir)
            {
                half3 data = half3(std, edgeDirection(left, right));

                aWY += weightY_edgeDir(pl.x,      pl.y + 1.0, upDown.x, data);
                aWY += weightY_edgeDir(pl.x - 1.0, pl.y + 1.0, upDown.y, data);
                aWY += weightY_edgeDir(pl.x - 1.0, pl.y - 2.0, upDown.z, data);
                aWY += weightY_edgeDir(pl.x,      pl.y - 2.0, upDown.w, data);
                aWY += weightY_edgeDir(pl.x + 1.0, pl.y - 1.0, left.x,   data);
                aWY += weightY_edgeDir(pl.x,      pl.y - 1.0, left.y,   data);
                aWY += weightY_edgeDir(pl.x,      pl.y,       left.z,   data);
                aWY += weightY_edgeDir(pl.x + 1.0, pl.y,       left.w,   data);
                aWY += weightY_edgeDir(pl.x - 1.0, pl.y - 1.0, right.x,  data);
                aWY += weightY_edgeDir(pl.x - 2.0, pl.y - 1.0, right.y,  data);
                aWY += weightY_edgeDir(pl.x - 2.0, pl.y,       right.z,  data);
                aWY += weightY_edgeDir(pl.x - 1.0, pl.y,       right.w,  data);
            }
            else
            {
                half data = std;

                aWY += weightY(pl.x,      pl.y + 1.0, upDown.x, data);
                aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, data);
                aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, data);
                aWY += weightY(pl.x,      pl.y - 2.0, upDown.w, data);
                aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x,   data);
                aWY += weightY(pl.x,      pl.y - 1.0, left.y,   data);
                aWY += weightY(pl.x,      pl.y,       left.z,   data);
                aWY += weightY(pl.x + 1.0, pl.y,       left.w,   data);
                aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x,  data);
                aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y,  data);
                aWY += weightY(pl.x - 2.0, pl.y,       right.z,  data);
                aWY += weightY(pl.x - 1.0, pl.y,       right.w,  data);
            }

            // Final luma reconstruction
            half finalY = aWY.y / aWY.x;

            half max4 = max(max(left.y, left.z), max(right.x, right.w));
            half min4 = min(min(left.y, left.z), min(right.x, right.w));
            finalY = clamp(edgeSharpness * finalY, min4, max4);

            half deltaY = finalY - pix.w;

            // Smooth high-contrast input
            deltaY = clamp(deltaY, half(-23.0 / 255.0), half(23.0 / 255.0));

            // Apply delta to all channels
            pix.x = saturate(pix.x + deltaY);
            pix.y = saturate(pix.y + deltaY);
            pix.z = saturate(pix.z + deltaY);
        }
    }

    pix.w = 1.0; // Alpha not used
}

// ── Compute shader entry point ──

[numthreads(8, 8, 1)]
void CS_main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_displayW || dtid.y >= g_displayH)
        return;

    // UV centered on pixel
    float2 uv = (float2(dtid.xy) + 0.5)
              / float2(g_displayW, g_displayH);

    // ViewportInfo: xy = 1/renderSize, zw = renderSize
    float4 ViewportInfo = float4(
        1.0 / float(g_renderW),
        1.0 / float(g_renderH),
        float(g_renderW),
        float(g_renderH));

    half4 OutColor;
    SgsrYuvH(OutColor, uv, ViewportInfo);
    OutputTexture[dtid.xy] = OutColor;
}
