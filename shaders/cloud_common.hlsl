#ifndef ENGINE_CLOUD_COMMON
#define ENGINE_CLOUD_COMMON

float CloudHash(float3 p) {
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float CloudNoise(float3 x) {
    float3 p = floor(x), f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(CloudHash(p), CloudHash(p + float3(1,0,0)), f.x),
                          lerp(CloudHash(p + float3(0,1,0)), CloudHash(p + float3(1,1,0)), f.x), f.y),
                lerp(lerp(CloudHash(p + float3(0,0,1)), CloudHash(p + float3(1,0,1)), f.x),
                     lerp(CloudHash(p + float3(0,1,1)), CloudHash(p + float3(1,1,1)), f.x), f.y), f.z);
}

float CloudFbm(float3 p) {
    float result = 0.0f, weight = 0.5f;
    [unroll] for (int i = 0; i < 5; ++i) {
        result += weight * CloudNoise(p);
        p *= 2.0f;
        weight *= 0.5f;
    }
    return result;
}

float2 CloudLayerBounds(float density) {
    float storm = saturate((density - 0.85f) / 1.80f);
    float base = lerp(850.0f, 550.0f, storm);
    return float2(base, base + lerp(1000.0f, 1350.0f, storm));
}

// Shared by visible clouds and surface lighting: same shape, wind and extinction.
static const float CloudExtinction = 0.0075f;
// Weather-scale coverage and height profile are shared with raster/DXR shadows.
float SampleCloudBody(float3 p, float time, float density) {
    float2 layer = CloudLayerBounds(density);
    float h = (p.y - layer.x) / (layer.y - layer.x);
    if (h <= 0 || h >= 1 || density <= 0) return 0;
    float storm = saturate((density - 0.85) / 1.8);
    float2 wind = time * float2(4.0 + 4.0 * storm, 1.6 + 2.0 * storm);
    float2 xz = p.xz + wind;
    float weather = CloudNoise(float3(xz * 0.00024, 11.0).xzy);
    float coverage = saturate(0.40 + density * 0.15 + (weather - 0.5) * 0.55);
    float3 q = float3(xz.x * 0.0012, h * 2.8, xz.y * 0.0012);
    float billow = CloudNoise(q) * 0.62 + CloudNoise(q * 2.07 + 17.3) * 0.26
                 + CloudNoise(q * 4.11 + 37.1) * 0.12;
    float profile = smoothstep(0.0, 0.10, h) * (1.0 - smoothstep(0.58, 1.0, h));
    float threshold = 0.63 - coverage * 0.35 + (1.0 - profile) * 0.22;
    return saturate((billow - threshold) * 3.4) * profile * density * 1.45;
}
float SampleCloudDensity(float3 p, float time, float density) {
    float body = SampleCloudBody(p, time, density);
    if (body <= 0.001) return 0;
    float storm = saturate((density - 0.85) / 1.8);
    float3 q = p + float3(time * (4.0 + 4.0 * storm), 0, time * (1.6 + 2.0 * storm));
    float erosion = CloudNoise(q * 0.006) * 0.7 + CloudNoise(q * 0.015 + 27.3) * 0.3;
    float edge = 1.0 - saturate(body / max(density, 0.01));
    return max(0, body - (1.0 - erosion) * edge * 0.26 * density);
}

float GetCloudShadow(float3 worldPos, float3 lightDir, float time, float density) {
    if (density <= 0.001f || lightDir.y >= -0.01f) return 1.0f;
    float3 sunRay = normalize(-lightDir);
    float2 layer = CloudLayerBounds(density);
    float entry = max((layer.x - worldPos.y) / sunRay.y, 0.0f);
    float exit = max((layer.y - worldPos.y) / sunRay.y, 0.0f);
    if (exit <= entry) return 1.0f;
    const int steps = 8;
    float stepSize = (exit - entry) / steps;
    float opticalDepth = 0.0f;
    [unroll] for (int i = 0; i < steps; ++i) {
        float3 p = worldPos + sunRay * (entry + (i + 0.5f) * stepSize);
        opticalDepth += SampleCloudBody(p, time, density) * stepSize * CloudExtinction;
    }
    return exp(-opticalDepth);
}
#endif
