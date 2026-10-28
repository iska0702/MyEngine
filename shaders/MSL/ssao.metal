#include <metal_stdlib>
using namespace metal;

struct SSAOConstants {
    float4x4 projection;
    float4x4 invProjection;
    float4x4 view;
    float4   samples[32];
    float4   noiseScale;
    float2   uvScale;
    float    radius;
    float    bias;
    float    intensity;
    float    padding;
};

struct SSAOBlurConstants {
    float2 texelSize;
    float2 padding;
};

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

fragment float4 PSSSAO(VertexOut in [[stage_in]],
                       constant SSAOConstants& cb [[buffer(0)]],
                       texture2d<float> depthTex  [[texture(0)]],
                       texture2d<float> normalTex [[texture(1)]],
                       sampler samp               [[sampler(0)]]) {
    float depth = depthTex.sample(samp, in.uv).r;
    if (depth >= 1.0f) {
        return float4(1.0f);
    }

    // Reconstruct View Space Position
    float4 clipPos = float4(in.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
    float4 viewPos = cb.invProjection * clipPos;
    viewPos /= viewPos.w;

    float3 normal = normalTex.sample(samp, in.uv).xyz * 2.0f - 1.0f;
    normal = normalize((cb.view * float4(normal, 0.0f)).xyz);

    float occlusion = 0.0f;
    for (int i = 0; i < 32; ++i) {
        float3 sampleDir = cb.samples[i].xyz;
        if (dot(sampleDir, normal) < 0.0f) {
            sampleDir = -sampleDir;
        }

        float3 samplePos = viewPos.xyz + sampleDir * cb.radius;
        float4 offset = cb.projection * float4(samplePos, 1.0f);
        offset.xy /= offset.w;
        float2 sampleUV = offset.xy * float2(0.5f, -0.5f) + 0.5f;

        float sampleDepth = depthTex.sample(samp, sampleUV).r;
        float4 sampleClip = float4(sampleUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), sampleDepth, 1.0f);
        float4 sampleView = cb.invProjection * sampleClip;
        sampleView /= sampleView.w;

        float rangeCheck = smoothstep(0.0f, 1.0f, cb.radius / abs(viewPos.z - sampleView.z));
        occlusion += (sampleView.z >= samplePos.z + cb.bias ? 1.0f : 0.0f) * rangeCheck;
    }

    occlusion = 1.0f - (occlusion / 32.0f) * cb.intensity;
    return float4(saturate(occlusion));
}

fragment float4 PSBlur(VertexOut in [[stage_in]],
                       constant SSAOBlurConstants& cb [[buffer(0)]],
                       texture2d<float> ssaoTex [[texture(0)]],
                       sampler samp             [[sampler(0)]]) {
    float result = 0.0f;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float2 offset = float2(x, y) * cb.texelSize;
            result += ssaoTex.sample(samp, in.uv + offset).r;
        }
    }
    return float4(result / 25.0f);
}
