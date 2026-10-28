#include <metal_stdlib>
using namespace metal;

struct PointLightData {
    packed_float3 position;
    float  range;
    packed_float3 color;
    float  intensity;
};

struct SpotLightData {
    packed_float3 position;
    float  range;
    packed_float3 direction;
    float  intensity;
    packed_float3 color;
    float  innerConeCos;
    float  outerConeCos;
    float  falloff;
    float  enabled;
    float  pad;
};

struct SceneConstantBuffer {
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float4x4 CleanViewProj;
    float4x4 PrevCleanViewProj;
    float4x4 Model;
    float4x4 SunViewProj;
    packed_float3 CameraPos;
    float    Pad1;
    packed_float3 LightDir;
    float    Pad2;
    packed_float3 LightColor;
    float    Pad3;
    float2   ScreenRes;
    float    tanHalfFov;
    float    screenHeight;
    uint     startIndexLocation;
    int      baseVertexLocation;
    uint     triangleCount;
    uint     indexFormat;
    uint     meshletCount;
    uint     meshletStart;
    uint     albedoIndex;
    uint     normalIndex;
    uint     rmaIndex;
    uint     aoIndex;
    uint     emissiveIndex;
    uint     padIndices;
    packed_float3 matAlbedo;
    float    matRoughness;
    float    matMetallic;
    float    matReflectance;
    float    matEmission;
    float    matIOR;
    float2   uvTiling;
    float    rainWetness;
    float    rainPuddles;
    float    cloudDensity;
    float    cloudTime;
    uint     numPointLights;
    uint     numSpotLights;
    float    padLights1;
    float    padLights2;
    float2 lightArrayPadding;
    PointLightData pointLights[16];
    SpotLightData  spotLights[16];
};

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float4 color;
    float2 uv;
    float4 curPos;
    float4 prevPos;
};

struct GBufferOut {
    float4 sceneColor   [[color(0)]];
    float4 velocity     [[color(1)]];
    float4 normalRough  [[color(2)]];
};

// Vertex Shader
vertex VertexOut VSMain(VertexIn in [[stage_in]],
                        constant SceneConstantBuffer& cb [[buffer(1)]]) {
    VertexOut out;
    float4 worldPos = cb.Model * float4(in.position, 1.0f);
    out.position = cb.ViewProj * worldPos;
    out.worldPos = worldPos.xyz;
    out.normal = normalize((cb.Model * float4(in.normal, 0.0f)).xyz);
    out.color = in.color;
    out.uv = in.color.zw * cb.uvTiling;
    out.curPos = cb.CleanViewProj * worldPos;
    out.prevPos = cb.PrevCleanViewProj * worldPos;
    return out;
}

// PBR Helper Functions
constant float PI = 3.14159265358979323846f;

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;
    return min(a2 / max(denom, 0.0001f), 100.0f);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

// Fragment Shader
fragment GBufferOut PSMain(VertexOut in [[stage_in]],
                           constant SceneConstantBuffer& cb [[buffer(1)]],
                           texture2d<float> albedoTex [[texture(0)]],
                           texture2d<float> normalTex [[texture(1)]],
                           texture2d<float> rmaTex [[texture(2)]],
                           sampler samp [[sampler(0)]],
                           sampler linearSamp [[sampler(1)]]) {
    GBufferOut out;

    float3 N = normalize(in.normal);
    float3 V = normalize(cb.CameraPos - in.worldPos);
    float3 L = normalize(-cb.LightDir);
    float3 H = normalize(V + L);

    // Material parameters
    float3 albedo = cb.matAlbedo;
    if (!is_null_texture(albedoTex)) {
        float4 sampled = albedoTex.sample(samp, in.uv);
        albedo *= sampled.rgb;
    }

    float roughness = cb.matRoughness;
    float metallic = cb.matMetallic;
    if (!is_null_texture(rmaTex)) {
        float4 rma = rmaTex.sample(samp, in.uv);
        roughness *= rma.g;
        metallic *= rma.b;
    }
    roughness = clamp(roughness, 0.04f, 1.0f);

    // PBR Direct Lighting
    float3 F0 = float3(0.04f);
    F0 = mix(F0, albedo, metallic);

    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.0001f;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (float3(1.0f) - kS) * (1.0f - metallic);
    float3 Lo = (kD * albedo / PI + specular) * cb.LightColor * NdotL;

    // Ambient lighting
    float3 ambient = float3(0.03f) * albedo;
    float3 finalColor = ambient + Lo + (cb.matEmission * albedo);

    // Output GBuffer
    out.sceneColor = float4(finalColor, 1.0f);

    // Motion Vectors (velocity)
    float2 curNDC = in.curPos.xy / in.curPos.w;
    float2 prevNDC = in.prevPos.xy / in.prevPos.w;
    float2 velocity = (curNDC - prevNDC) * float2(0.5f, -0.5f);
    out.velocity = float4(velocity, 0.0f, 1.0f);

    out.normalRough = float4(N * 0.5f + 0.5f, roughness);

    return out;
}
