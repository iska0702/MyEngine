#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut VSMain(uint vertexID [[vertex_id]]) {
    VertexOut out;
    out.uv = float2((vertexID << 1) & 2, vertexID & 2);
    out.position = float4(out.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return out;
}

// Interleaved Gradient Noise for film grain
float InterleavedGradientNoise(float2 screenPos) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return fract(magic.z * fract(dot(screenPos, magic.xy)));
}

// 5-Tap Bilinear Tent Bloom Filter
float3 SampleBloom(texture2d<float> tex, sampler samp, float2 uv, float2 texelSize) {
    float2 d = texelSize * 1.5f;
    float3 s0 = tex.sample(samp, uv).rgb;
    float3 s1 = tex.sample(samp, uv + float2(-d.x, -d.y)).rgb;
    float3 s2 = tex.sample(samp, uv + float2( d.x, -d.y)).rgb;
    float3 s3 = tex.sample(samp, uv + float2(-d.x,  d.y)).rgb;
    float3 s4 = tex.sample(samp, uv + float2( d.x,  d.y)).rgb;

    float3 bloom = s0 * 0.4f + (s1 + s2 + s3 + s4) * 0.15f;
    float lum = dot(bloom, float3(0.2126f, 0.7152f, 0.0722f));
    float softThreshold = clamp((lum - 0.85f) / max(lum, 0.001f), 0.0f, 1.0f);
    return max(bloom * softThreshold, 0.0f);
}

// ACES Fitted Tone Mapping Curve
float3 ACESFitted(float3 x) {
    x = max(x, 0.0f);
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

fragment float4 PSMain(VertexOut in [[stage_in]],
                       texture2d<float> sceneColor [[texture(0)]],
                       texture2d<float> ssaoTex    [[texture(1)]],
                       sampler samp                [[sampler(0)]]) {
    float2 texelSize = 1.0f / float2(sceneColor.get_width(), sceneColor.get_height());

    // Chromatic Aberration
    float2 centerOffset = in.uv - 0.5f;
    float dist = dot(centerOffset, centerOffset);
    float2 caOffset = centerOffset * dist * 0.008f;

    float r = sceneColor.sample(samp, in.uv - caOffset).r;
    float g = sceneColor.sample(samp, in.uv).g;
    float b = sceneColor.sample(samp, in.uv + caOffset).b;
    float3 color = float3(r, g, b);

    // Apply SSAO
    if (!is_null_texture(ssaoTex)) {
        float ssao = ssaoTex.sample(samp, in.uv).r;
        color *= ssao;
    }

    // Bloom
    float3 bloom = SampleBloom(sceneColor, samp, in.uv, texelSize);
    color += bloom * 0.25f;

    // ACES Tone Mapping
    color = ACESFitted(color);

    // Vignette
    float vignette = 1.0f - dist * 1.2f;
    vignette = clamp(vignette, 0.0f, 1.0f);
    color *= vignette;

    // Film Grain
    float grain = (InterleavedGradientNoise(in.position.xy) - 0.5f) * 0.02f;
    color += grain;

    // Gamma Correction
    color = pow(max(color, 0.0f), float3(1.0f / 2.2f));

    return float4(color, 1.0f);
}
