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

fragment float4 PSMain(VertexOut in [[stage_in]],
                       texture2d<float> sceneColor [[texture(0)]],
                       texture2d<float> reflection [[texture(1)]],
                       texture3d<float> fogAccum   [[texture(2)]],
                       texture2d<float> depthTex   [[texture(3)]],
                       sampler samp                [[sampler(0)]]) {
    float4 color = sceneColor.sample(samp, in.uv);
    float depth = depthTex.sample(samp, in.uv).r;

    // Composite SSR reflection
    if (!is_null_texture(reflection)) {
        float4 refl = reflection.sample(samp, in.uv);
        color.rgb += refl.rgb;
    }

    // Composite Volumetric Fog
    if (!is_null_texture(fogAccum)) {
        float fogZ = clamp(depth, 0.0f, 0.99f);
        float4 fog = fogAccum.sample(samp, float3(in.uv, fogZ));
        color.rgb = color.rgb * fog.a + fog.rgb;
    }

    return color;
}
