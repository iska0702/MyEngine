#include <metal_stdlib>
using namespace metal;

struct SkyboxConstants {
    float4x4 invViewProj;
    float4   lightColor;
    float4   lightDir;
    float4   skyColor;
};

struct VertexOut {
    float4 position [[position]];
    float3 viewRay;
};

vertex VertexOut VSMain(uint vertexID [[vertex_id]],
                        constant SkyboxConstants& cb [[buffer(0)]]) {
    VertexOut out;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    out.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);

    float4 nearPoint = cb.invViewProj * float4(out.position.xy, 0.0f, 1.0f);
    nearPoint /= nearPoint.w;
    float4 farPoint = cb.invViewProj * float4(out.position.xy, 1.0f, 1.0f);
    farPoint /= farPoint.w;

    out.viewRay = normalize(farPoint.xyz - nearPoint.xyz);
    return out;
}

fragment float4 PSMain(VertexOut in [[stage_in]],
                       constant SkyboxConstants& cb [[buffer(0)]]) {
    float3 dir = normalize(in.viewRay);
    float3 lightDir = normalize(-cb.lightDir.xyz);

    float t = saturate(dir.y);
    float tPow = pow(t, 0.6f);

    float sunElev = -cb.lightDir.y;
    float nightWeight = saturate((0.15f - dot(cb.lightColor.rgb, float3(0.333f))) * 8.0f);
    if (sunElev < 0.0f) {
        nightWeight = max(nightWeight, saturate(-sunElev * 5.0f));
    }
    float nonNightWeight = (1.0f - nightWeight) * saturate(sunElev * 5.0f + 0.1f);

    float sunsetWeight = saturate(smoothstep(0.35f, -0.05f, sunElev)) * nonNightWeight;
    float dayWeight = saturate(nonNightWeight - sunsetWeight);

    float3 dayZenith = float3(0.12f, 0.22f, 0.42f) * cb.lightColor.rgb;
    float3 dayHorizon = float3(0.35f, 0.45f, 0.60f) * cb.lightColor.rgb;
    float3 sunsetHorizon = float3(0.65f, 0.32f, 0.12f) * cb.lightColor.rgb;
    float3 sunsetZenith = float3(0.15f, 0.12f, 0.25f) * cb.lightColor.rgb;
    float3 nightZenith = float3(0.006f, 0.010f, 0.020f) * cb.lightColor.rgb * 2.0f;
    float3 nightHorizon = float3(0.012f, 0.018f, 0.035f) * cb.lightColor.rgb * 2.0f;

    float3 daySky = mix(dayHorizon, dayZenith, tPow);
    float3 sunsetSky = mix(sunsetHorizon, sunsetZenith, tPow);
    float3 nightSky = mix(nightHorizon, nightZenith, tPow);

    float3 sky = daySky * dayWeight + sunsetSky * sunsetWeight + nightSky * nightWeight;

    // Sun Disk
    float bodyDot = dot(dir, lightDir);
    if (nonNightWeight > 0.05f) {
        float sunDisk = smoothstep(0.999f, 0.9995f, bodyDot);
        sky += cb.lightColor.rgb * 8.0f * sunDisk * nonNightWeight;
    }
    // Moon Disk
    if (nightWeight > 0.05f) {
        float moonDisk = smoothstep(0.9982f, 0.9992f, bodyDot);
        sky += cb.lightColor.rgb * 3.0f * moonDisk * nightWeight;
    }

    return float4(sky, 1.0f);
}
