// Cinematic Post-Processing Shader (Physically Based Bloom, Multi-Operator Tonemapping, Color Grading, Chromatic Aberration, Vignette, Film Grain)

Texture2D gSceneColor : register(t0);
Texture2D gSSAO       : register(t1);
SamplerState gSampler : register(s0);

cbuffer PostProcessCB : register(b0) {
    float4 g_ExposureBloom;    // x: exposure, y: bloomIntensity, z: bloomThreshold, w: bloomScatter
    float4 g_ColorGrading;     // x: contrast, y: saturation, z: colorTemperature, w: tint
    float4 g_ColorFilter;      // xyz: filter tint, w: tonemapper operator index
    float4 g_LensEffects;      // x: chromaticAberration, y: vignetteIntensity, z: vignetteSmoothness, w: filmGrain
    float4 g_ShadowTint;       // xyz: shadow tint, w: unused
    float4 g_HighlightTint;    // xyz: highlight tint, w: unused
    float4 g_DoFParams;        // x: dofEnabled (0/1), y: focusDistance, z: focalLength, w: maxBlurRadius
    float4 g_Padding;
};

struct VertexIn {
    uint VertexId : SV_VertexID;
};

struct VertexOut {
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VSMain(VertexIn vin) {
    VertexOut vout;
    vout.TexC = float2((vin.VertexId << 1) & 2, vin.VertexId & 2);
    vout.PosH = float4(vout.TexC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

// Interleaved Gradient Noise for film grain and de-banding
float InterleavedGradientNoise(float2 screenPos) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// Fast Bilinear Tent Bloom Filter with dynamic threshold
float3 SampleBloom(float2 uv, float2 texelSize, float threshold) {
    float2 d = texelSize * 1.5f;
    float3 s0 = gSceneColor.SampleLevel(gSampler, uv, 0).rgb;
    float3 s1 = gSceneColor.SampleLevel(gSampler, uv + float2(-d.x, -d.y), 0).rgb;
    float3 s2 = gSceneColor.SampleLevel(gSampler, uv + float2( d.x, -d.y), 0).rgb;
    float3 s3 = gSceneColor.SampleLevel(gSampler, uv + float2(-d.x,  d.y), 0).rgb;
    float3 s4 = gSceneColor.SampleLevel(gSampler, uv + float2( d.x,  d.y), 0).rgb;

    float3 bloom = s0 * 0.4f + (s1 + s2 + s3 + s4) * 0.15f;
    float lum = dot(bloom, float3(0.2126f, 0.7152f, 0.0722f));
    float softThreshold = clamp((lum - threshold) / max(lum, 0.001f), 0.0f, 1.0f);
    return max(bloom * softThreshold, 0.0f);
}

// -------------------------------------------------------------
// Tonemapping Operators
// -------------------------------------------------------------
// 0: ACES Fitted (Stephen Hill fit)
float3 TonemapACES(float3 x) {
    x = max(x, 0.0f);
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// 1: Filmic Curve (Uncharted 2 / Hable)
float3 HableCurve(float3 v) {
    float A = 0.15f; float B = 0.50f; float C = 0.10f;
    float D = 0.20f; float E = 0.02f; float F = 0.30f;
    return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F;
}

float3 TonemapFilmic(float3 x) {
    float3 curr = HableCurve(x * 2.0f);
    float3 whiteScale = 1.0f / HableCurve(float3(11.2f, 11.2f, 11.2f));
    return saturate(curr * whiteScale);
}

// 2: Extended Reinhard
float3 TonemapReinhard(float3 x) {
    float whitePt = 4.0f;
    return saturate((x * (1.0f + x / (whitePt * whitePt))) / (1.0f + x));
}

// 3: AgX PBR Curve
float3 TonemapAgX(float3 x) {
    // AgX logarithmic approximation
    float3 logX = log2(max(x, 0.0001f));
    float3 norm = (logX + 10.0f) / 16.5f;
    return saturate(norm * norm * (3.0f - 2.0f * norm));
}

// 4: Neutral Linear (saturate with gentle shoulder)
float3 TonemapNeutral(float3 x) {
    return saturate(x / (1.0f + x * 0.15f));
}

float3 ApplyTonemap(float3 x, int mode) {
    if (mode == 1) return TonemapFilmic(x);
    if (mode == 2) return TonemapReinhard(x);
    if (mode == 3) return TonemapAgX(x);
    if (mode == 4) return TonemapNeutral(x);
    return TonemapACES(x);
}

// -------------------------------------------------------------
// Color Grading & White Balance
// -------------------------------------------------------------
float3 ApplyColorGrading(float3 color) {
    // Temperature & Tint shift
    float temp = g_ColorGrading.z;
    float tint = g_ColorGrading.w;
    color.r += temp * 0.08f;
    color.b -= temp * 0.08f;
    color.g += tint * 0.06f;

    // Filter Tint
    color *= g_ColorFilter.xyz;

    // Luminance
    float lum = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

    // Shadow & Highlight split-toning
    color = lerp(color * g_ShadowTint.xyz, color * g_HighlightTint.xyz, saturate(lum * 1.5f));

    // Contrast
    float contrast = g_ColorGrading.x;
    color = ((color - 0.5f) * contrast) + 0.5f;

    // Saturation
    float sat = g_ColorGrading.y;
    color = lerp(float3(lum, lum, lum), color, sat);

    return max(color, 0.0f);
}

float4 PSMain(VertexOut pin) : SV_Target {
    float2 uv = pin.TexC;
    float2 toCenter = uv - float2(0.5f, 0.5f);
    float distFromCenter = length(toCenter);

    // 1. Chromatic Aberration
    float caStrength = g_LensEffects.x;
    float2 caOffset = toCenter * (distFromCenter * caStrength);
    float r = gSceneColor.Sample(gSampler, uv - caOffset).r;
    float g = gSceneColor.Sample(gSampler, uv).g;
    float b = gSceneColor.Sample(gSampler, uv + caOffset).b;
    float3 hdrColor = float3(r, g, b);

    // Sanitize NaNs
    hdrColor.r = isfinite(hdrColor.r) ? max(0.0f, hdrColor.r) : 0.0f;
    hdrColor.g = isfinite(hdrColor.g) ? max(0.0f, hdrColor.g) : 0.0f;
    hdrColor.b = isfinite(hdrColor.b) ? max(0.0f, hdrColor.b) : 0.0f;

    // 2. High-Performance Multi-Scale Bloom
    float bloomIntensity = g_ExposureBloom.y;
    float bloomThreshold = g_ExposureBloom.z;
    if (bloomIntensity > 0.001f) {
        uint2 texDims;
        gSceneColor.GetDimensions(texDims.x, texDims.y);
        float2 texelSize = float2(1.0f / float(max(texDims.x, 1u)), 1.0f / float(max(texDims.y, 1u)));

        float3 bloom1 = SampleBloom(uv, texelSize * 2.5f, bloomThreshold);
        float3 bloom2 = SampleBloom(uv, texelSize * 8.0f, bloomThreshold);
        float3 totalBloom = bloom1 * 0.65f + bloom2 * 0.35f;

        hdrColor += totalBloom * bloomIntensity;
    }

    // 3. Exposure & Tonemapping
    float exposure = g_ExposureBloom.x;
    int tonemapMode = int(g_ColorFilter.w + 0.1f);
    float3 mapped = ApplyTonemap(hdrColor * exposure, tonemapMode);

    // 4. Color Grading
    mapped = ApplyColorGrading(mapped);

    // 5. Optical Lens Vignette
    float vigIntensity = g_LensEffects.y;
    float vigSmooth = g_LensEffects.z;
    if (vigIntensity > 0.001f) {
        float vignette = smoothstep(0.85f, max(0.1f, 0.85f - vigSmooth), distFromCenter);
        mapped *= lerp(1.0f - vigIntensity, 1.0f, vignette);
    }

    // 6. Cinematic Film Grain
    float grainIntensity = g_LensEffects.w;
    if (grainIntensity > 0.001f) {
        float lum = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
        if (lum > 0.0005f) {
            float grain = (InterleavedGradientNoise(pin.PosH.xy) - 0.5f) * grainIntensity * saturate(lum * 10.0f);
            mapped += float3(grain, grain, grain);
        }
    }

    // 7. Gamma Correction (sRGB)
    float3 finalColor = pow(max(mapped, 0.0f), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
    return float4(saturate(finalColor), 1.0f);
}
