//============================================================================
//  SGSR1 RCAS — Standalone Sharpening for Path B
//  Used for non-DLSS games: render at native res, apply sharpening only.
//
//  This is NOT the main upscaling path — that uses sgsr1_official.hlsl.
//  RCAS is invoked separately when only sharpening is needed.
//============================================================================

cbuffer RCASConstants : register(b0)
{
    uint  g_width;       // Display width
    uint  g_height;      // Display height
    float g_sharpness;   // 0.0 = none, 2.0 = default SGSR1 sharpness
    uint  g_padding;
};

Texture2D<half4>     InputTexture   : register(t0);
RWTexture2D<half4>   OutputTexture  : register(u0);
SamplerState         samLinearClamp : register(s0);

// ── Simplified RCAS-style adaptive sharpening ──

half4 RCASFilter(half2 center, half2 pixelSize)
{
    // Sample 5-tap cross
    half4 n = InputTexture.SampleLevel(samLinearClamp,
        center + half2(0, -pixelSize.y), 0);
    half4 s = InputTexture.SampleLevel(samLinearClamp,
        center + half2(0,  pixelSize.y), 0);
    half4 w = InputTexture.SampleLevel(samLinearClamp,
        center + half2(-pixelSize.x, 0), 0);
    half4 e = InputTexture.SampleLevel(samLinearClamp,
        center + half2( pixelSize.x, 0), 0);
    half4 c = InputTexture.SampleLevel(samLinearClamp,
        center, 0);

    // Luma weights (ITU-R BT.709)
    static const half3 lw = half3(0.2126, 0.7152, 0.0722);

    half lN = dot(n.rgb, lw);
    half lS = dot(s.rgb, lw);
    half lW = dot(w.rgb, lw);
    half lE = dot(e.rgb, lw);
    half lC = dot(c.rgb, lw);

    // Local contrast range
    half rMax  = max(lC, max(max(lN, lS), max(lW, lE)));
    half rMin  = min(lC, min(min(lN, lS), min(lW, lE)));
    half range = rMax - rMin;

    // Attenuate sharpening in high-contrast areas
    half att    = 1.0 - clamp(range * g_sharpness, 0.0, 1.0);
    half weight = att * att;

    // Blend: center vs neighbor average
    half3 blended = (n.rgb + s.rgb + w.rgb + e.rgb) * half(0.25);
    half3 result  = lerp(blended, c.rgb, weight);

    return half4(saturate(result), 1.0);
}

[numthreads(8, 8, 1)]
void CS_main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= g_width || dtid.y >= g_height)
        return;

    half2 pixelSize = half2(1.0 / half(g_width), 1.0 / half(g_height));
    half2 uv = (half2(dtid.xy) + 0.5) * pixelSize;

    OutputTexture[dtid.xy] = RCASFilter(uv, pixelSize);
}
