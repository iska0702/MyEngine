#include <metal_stdlib>
using namespace metal;

struct RainCB {
    float4x4 g_ViewProj;
    float4   g_LightColorAndIntensity; // rgb = light, a = lightning
    float4   g_CameraPosAndTime;       // xyz = camPos, w = time
    float4   g_LightDir;               // xyz = lightDir, w = unused
};

struct VSInput {
    float3 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct PSInput {
    float4 position [[position]];
    float2 uv;
    float4 color;
    float3 worldPos;
};

vertex PSInput VSStreak(VSInput in [[stage_in]],
                        constant RainCB& cb [[buffer(1)]]) {
    PSInput out;
    out.position = cb.g_ViewProj * float4(in.position, 1.0f);
    out.uv = in.uv;
    out.color = in.color;
    out.worldPos = in.position;
    return out;
}

fragment float4 PSStreak(PSInput in [[stage_in]],
                         constant RainCB& cb [[buffer(1)]]) {
    // 1. Aerodynamic cylindrical teardrop cross-section
    float xDist = (in.uv.x - 0.5f) * 2.0f;
    float r2 = xDist * xDist;
    if (r2 > 1.0f) discard_fragment();

    // Optical thickness of water cylinder (Fresnel highlight)
    float waterThickness = sqrt(1.0f - r2);
    float coreGlow = pow(waterThickness, 1.8f);
    float edgeSoftness = smoothstep(1.0f, 0.2f, abs(xDist));

    // 2. Vertical streak profile
    float yProfile = pow(in.uv.y, 0.85f);
    float headBulb = smoothstep(0.75f, 1.0f, in.uv.y) * 0.25f;
    float alpha = (coreGlow * 0.40f + edgeSoftness * 0.25f + headBulb) * yProfile * in.color.a * 0.55f;

    // 3. Distance fade
    float distToCam = length(in.worldPos - cb.g_CameraPosAndTime.xyz);
    alpha *= smoothstep(0.20f, 0.6f, distToCam);

    // 4. Directional Sun & Sky Lighting with Forward Mie Scattering
    float3 lightDir = normalize(cb.g_LightDir.xyz);
    float3 viewDir = normalize(cb.g_CameraPosAndTime.xyz - in.worldPos);
    float forwardScatter = pow(saturate(dot(viewDir, -lightDir)), 4.0f) * 1.2f;

    float3 sunGlow = cb.g_LightColorAndIntensity.rgb * (0.65f + forwardScatter);
    float3 skyAmbient = float3(0.55f, 0.65f, 0.78f) * 0.75f;
    float3 lightningGlow = float3(1.6f, 1.7f, 2.0f) * (cb.g_LightColorAndIntensity.a * 2.2f);

    float3 dropRgb = (sunGlow + skyAmbient + lightningGlow) * (0.75f + coreGlow * 0.35f);

    return float4(dropRgb, saturate(alpha));
}

vertex PSInput VSSplash(VSInput in [[stage_in]],
                        constant RainCB& cb [[buffer(1)]]) {
    PSInput out;
    out.position = cb.g_ViewProj * float4(in.position, 1.0f);
    out.uv = in.uv;
    out.color = in.color;
    out.worldPos = in.position;
    return out;
}

fragment float4 PSSplash(PSInput in [[stage_in]],
                         constant RainCB& cb [[buffer(1)]]) {
    float2 centerOffset = in.uv - 0.5f;
    float dist = length(centerOffset) * 2.0f;
    if (dist > 1.0f) discard_fragment();

    float ring = sin(dist * 12.0f - cb.g_CameraPosAndTime.w * 8.0f) * 0.5f + 0.5f;
    float fade = (1.0f - dist) * in.color.a;

    float3 lightColor = cb.g_LightColorAndIntensity.rgb + float3(1.5f) * cb.g_LightColorAndIntensity.a;
    return float4(lightColor * 0.8f, saturate(ring * fade * 0.6f));
}
