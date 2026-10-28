#include <metal_stdlib>
using namespace metal;

struct SSRConstants {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4   cameraPos;
    float4   params; // x = maxDistance, y = stepSize, z = maxSteps, w = thickness
};

kernel void CSMain(uint2 id [[thread_position_in_grid]],
                   constant SSRConstants& cb [[buffer(0)]],
                   texture2d<float, access::read> colorTex [[texture(0)]],
                   texture2d<float, access::read> depthTex [[texture(1)]],
                   texture2d<float, access::read> normalTex [[texture(2)]],
                   texture2d<float, access::write> outRefl [[texture(3)]]) {
    if (id.x >= outRefl.get_width() || id.y >= outRefl.get_height()) {
        return;
    }

    float depth = depthTex.read(id).r;
    if (depth >= 1.0f) {
        outRefl.write(float4(0.0f), id);
        return;
    }

    float4 normSample = normalTex.read(id);
    float3 N = normalize(normSample.xyz * 2.0f - 1.0f);
    float roughness = normSample.w;

    if (roughness > 0.8f) {
        outRefl.write(float4(0.0f), id);
        return;
    }

    // Screen UV to World Position
    float2 uv = (float2(id) + 0.5f) / float2(outRefl.get_width(), outRefl.get_height());
    float4 clip = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), depth, 1.0f);
    float4 worldPos = cb.invViewProj * clip;
    worldPos /= worldPos.w;

    float3 V = normalize(worldPos.xyz - cb.cameraPos.xyz);
    float3 R = reflect(V, N);

    // Raymarch in world space projected to screen
    float3 rayPos = worldPos.xyz + R * 0.05f;
    float maxDist = cb.params.x;
    float stepSize = cb.params.y;
    int maxSteps = int(cb.params.z);

    float4 hitColor = float4(0.0f);

    for (int i = 0; i < maxSteps; ++i) {
        rayPos += R * stepSize;
        float4 proj = cb.viewProj * float4(rayPos, 1.0f);
        proj /= proj.w;
        if (proj.x < -1.0f || proj.x > 1.0f || proj.y < -1.0f || proj.y > 1.0f || proj.z < 0.0f || proj.z > 1.0f) {
            break;
        }

        float2 hitUV = proj.xy * float2(0.5f, -0.5f) + 0.5f;
        uint2 hitCoord = uint2(hitUV * float2(depthTex.get_width(), depthTex.get_height()));
        float sceneDepth = depthTex.read(hitCoord).r;

        if (proj.z >= sceneDepth && (proj.z - sceneDepth) < cb.params.w) {
            float fade = 1.0f - float(i) / float(maxSteps);
            hitColor = float4(colorTex.read(hitCoord).rgb * fade * (1.0f - roughness), 1.0f);
            break;
        }
    }

    outRefl.write(hitColor, id);
}
