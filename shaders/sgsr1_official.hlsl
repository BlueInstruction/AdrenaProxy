// ============================================================
// AdrenaProxy — SGSR1 Official Compute Shader
// Based on Qualcomm Snapdragon Game Super Resolution v1
// BSD-3-Clause License — Copyright (c) Qualcomm Technologies, Inc.
//
// Adapted from sgsr1_shader_mobile.hlsl + sgsr1_mobile.h
// Converted from pixel shader to D3D12 compute shader
// Uses 12-tap Lanczos-like scaling + edge-adaptive sharpening
// ============================================================

SamplerState      g_sampler  : register(s0);
Texture2D<half4>  g_input    : register(t0);
RWTexture2D<float4> g_output : register(u0);

cbuffer Constants : register(b0)
{
    uint2  g_renderSize;     // Source (low-res) dimensions
    uint2  g_displaySize;    // Target (display) dimensions
    float  g_sharpness;      // EdgeSharpness (1.0 - 2.0, default 2.0)
    uint   g_frameCount;
    float  g_edgeThreshold;  // EdgeThreshold (8.0/255.0 default, 4.0/255.0 for VR)
    uint   g_operationMode;  // 1=RGBA, 3=RGBY, 4=LERP
    uint   g_useEdgeDir;     // 0=off, 1=use edge direction
    float2 g_padding;
};

// ─── fastLanczos2 — Cubic polynomial approximation ────────
// Avoids sin() for VKD3D/Turnip compatibility
half fastLanczos2(half x)
{
    half wA = x - half(4.0);
    half wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

// ─── weightY without edge direction ──────────────────────
half2 weightY(half dx, half dy, half c, half std)
{
    half x = ((dx * dx) + (dy * dy)) * half(0.5) + clamp(abs(c) * std, 0.0, 1.0);
    half w = fastLanczos2(x);
    return half2(w, w * c);
}

// ─── edgeDirection ───────────────────────────────────────
half2 edgeDirection(half4 left, half4 right)
{
    half RxLz = (right.x + (-left.z));
    half RwLy = (right.w + (-left.y));
    half2 delta;
    delta.x = (RxLz + RwLy);
    delta.y = (RxLz + (-RwLy));
    half lengthInv = rsqrt((delta.x * delta.x + 3.075740e-05) + (delta.y * delta.y));
    return half2(delta.x * lengthInv, delta.y * lengthInv);
}

// ─── weightY with edge direction ─────────────────────────
half2 weightYEdgeDir(half dx, half dy, half c, half3 data)
{
    half std = data.x;
    half2 dir = data.yz;

    half edgeDis = ((dx * dir.y) + (dy * dir.x));
    half x = (((dx * dx) + (dy * dy)) +
              ((edgeDis * edgeDis) * ((clamp(((c * c) * std), 0.0, 1.0) * 0.7) + -1.0)));
    half w = fastLanczos2(x);
    return half2(w, w * c);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= g_displaySize))
        return;

    int mode = (int)g_operationMode;
    half edgeThreshold = (half)g_edgeThreshold;
    half edgeSharpness = (half)g_sharpness;

    // ── ViewportInfo: xy = pixelSize (1/w, 1/h), zw = textureSize (w, h) ──
    float2 pixelSize  = 1.0 / float2(g_renderSize);
    float2 texSize    = float2(g_renderSize);

    // UV in display space → source space
    float2 uv = (float2(DTid.xy) + 0.5) / float2(g_displaySize);

    // Sample center pixel
    half4 color;
    if (mode == 1)
        color.xyz = g_input.SampleLevel(g_sampler, uv, 0).xyz;
    else
        color.xyzw = g_input.SampleLevel(g_sampler, uv, 0).xyzw;

    // ── SGSR1 spatial upscaling + sharpening ──
    // Convert UV to source image coordinates
    float2 imgCoord = ((uv * texSize) + float2(-0.5, 0.5));
    float2 imgCoordPixel = floor(imgCoord);
    float2 coord = (imgCoordPixel * pixelSize);
    half2 pl = (imgCoord + (-imgCoordPixel));

    // Gather 4 neighbors: left = GatherRed at pixel-aligned coord
    half4 left;
    if (mode == 0)
        left = g_input.GatherRed(g_sampler, coord);
    else if (mode == 1)
        left = g_input.GatherRed(g_sampler, coord);
    else if (mode == 2)
        left = g_input.GatherBlue(g_sampler, coord);
    else
        left = g_input.GatherAlpha(g_sampler, coord);

    // Edge voting: check if this pixel is on an edge
    half edgeVote = abs(left.z - left.y) +
                    abs(color[mode] - left.y) +
                    abs(color[mode] - left.z);

    if (edgeVote > edgeThreshold)
    {
        coord.x += pixelSize.x;

        // Gather right column
        half4 right;
        if (mode == 0)
            right = g_input.GatherRed(g_sampler, coord + float2(pixelSize.x, 0.0));
        else if (mode == 1)
            right = g_input.GatherRed(g_sampler, coord + float2(pixelSize.x, 0.0));
        else if (mode == 2)
            right = g_input.GatherBlue(g_sampler, coord + float2(pixelSize.x, 0.0));
        else
            right = g_input.GatherAlpha(g_sampler, coord + float2(pixelSize.x, 0.0));

        // Gather up and down rows
        half4 upDown;
        half4 topSample;
        half4 bottomSample;

        if (mode == 0) {
            topSample = g_input.GatherRed(g_sampler, coord + float2(0.0, -pixelSize.y));
            bottomSample = g_input.GatherRed(g_sampler, coord + float2(0.0, pixelSize.y));
        } else if (mode == 1) {
            topSample = g_input.GatherRed(g_sampler, coord + float2(0.0, -pixelSize.y));
            bottomSample = g_input.GatherRed(g_sampler, coord + float2(0.0, pixelSize.y));
        } else if (mode == 2) {
            topSample = g_input.GatherBlue(g_sampler, coord + float2(0.0, -pixelSize.y));
            bottomSample = g_input.GatherBlue(g_sampler, coord + float2(0.0, pixelSize.y));
        } else {
            topSample = g_input.GatherAlpha(g_sampler, coord + float2(0.0, -pixelSize.y));
            bottomSample = g_input.GatherAlpha(g_sampler, coord + float2(0.0, pixelSize.y));
        }
        upDown.xy = topSample.wz;
        upDown.zw = bottomSample.yx;

        // Compute mean and center the neighborhood
        half mean = (left.y + left.z + right.x + right.w) * half(0.25);
        left   = left   - half4(mean, mean, mean, mean);
        right  = right  - half4(mean, mean, mean, mean);
        upDown = upDown - half4(mean, mean, mean, mean);
        color.w = color[mode] - mean;

        // Compute standard deviation
        half sum = (((((abs(left.x) + abs(left.y)) + abs(left.z)) + abs(left.w)) +
                    (((abs(right.x) + abs(right.y)) + abs(right.z)) + abs(right.w))) +
                   (((abs(upDown.x) + abs(upDown.y)) + abs(upDown.z)) + abs(upDown.w)));
        half sumMean = half(1.014185e+01) / sum;
        half std = (sumMean * sumMean);

        // ── 12-tap Lanczos-like weighted average ──
        half2 aWY;

        if (g_useEdgeDir != 0)
        {
            // With edge direction
            half3 data = half3(std, edgeDirection(left, right));

            aWY  = weightYEdgeDir(pl.x, pl.y + 1.0, upDown.x, data);
            aWY += weightYEdgeDir(pl.x - 1.0, pl.y + 1.0, upDown.y, data);
            aWY += weightYEdgeDir(pl.x - 1.0, pl.y - 2.0, upDown.z, data);
            aWY += weightYEdgeDir(pl.x, pl.y - 2.0, upDown.w, data);
            aWY += weightYEdgeDir(pl.x + 1.0, pl.y - 1.0, left.x, data);
            aWY += weightYEdgeDir(pl.x, pl.y - 1.0, left.y, data);
            aWY += weightYEdgeDir(pl.x, pl.y, left.z, data);
            aWY += weightYEdgeDir(pl.x + 1.0, pl.y, left.w, data);
            aWY += weightYEdgeDir(pl.x - 1.0, pl.y - 1.0, right.x, data);
            aWY += weightYEdgeDir(pl.x - 2.0, pl.y - 1.0, right.y, data);
            aWY += weightYEdgeDir(pl.x - 2.0, pl.y, right.z, data);
            aWY += weightYEdgeDir(pl.x - 1.0, pl.y, right.w, data);
        }
        else
        {
            // Without edge direction (default)
            aWY  = weightY(pl.x, pl.y + 1.0, upDown.x, std);
            aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, std);
            aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, std);
            aWY += weightY(pl.x, pl.y - 2.0, upDown.w, std);
            aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, std);
            aWY += weightY(pl.x, pl.y - 1.0, left.y, std);
            aWY += weightY(pl.x, pl.y, left.z, std);
            aWY += weightY(pl.x + 1.0, pl.y, left.w, std);
            aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, std);
            aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, std);
            aWY += weightY(pl.x - 2.0, pl.y, right.z, std);
            aWY += weightY(pl.x - 1.0, pl.y, right.w, std);
        }

        half finalY = aWY.y / aWY.x;

        // Clamp to neighborhood min/max (anti-ringing)
        half maxY = max(max(left.y, left.z), max(right.x, right.w));
        half minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(edgeSharpness * finalY, minY, maxY);

        half deltaY = finalY - color.w;

        // Smooth high contrast input
        deltaY = clamp(deltaY, half(-23.0 / 255.0), half(23.0 / 255.0));

        color.x = saturate((color.x + deltaY));
        color.y = saturate((color.y + deltaY));
        color.z = saturate((color.z + deltaY));
    }

    color.w = 1.0;
    g_output[DTid.xy] = float4(color);
}
