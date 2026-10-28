// Astra Volumetric Fog Compute Shader
// Performs light injection and temporal accumulation in a 3D voxel grid

struct PointLightData {
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

struct SpotLightData {
    float3 position;
    float  range;
    float3 direction;
    float  intensity;
    float3 color;
    float  innerConeCos;
    float  outerConeCos;
    float  falloff;
    float  enabled;
    float  pad;
};

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float4x4 CleanViewProj;
    float4x4 PrevCleanViewProj;
    float4x4 Model;
    float4x4 SunViewProj;
    float3   CameraPos;
    float    Pad1;
    float3   LightDir;
    float    Pad2;
    float3   LightColor;
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
    float3   matAlbedo;
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
    PointLightData pointLights[16];
    SpotLightData  spotLights[16];
};

Texture2D<float4> g_AstraGIRadiance : register(t0); // Indirect light from Astra GI

RWTexture3D<float4> g_FogVolume : register(u0);
RWTexture3D<float4> g_AccumulationVolume : register(u1);

// High-Quality Volumetric Cloud Shadow with 3D Noise & Optical Depth Integration
float cloudHash(float3 p) {
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float cloudNoise(float3 x) {
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    
    return lerp(lerp(lerp(cloudHash(p + float3(0, 0, 0)), cloudHash(p + float3(1, 0, 0)), f.x),
                     lerp(cloudHash(p + float3(0, 1, 0)), cloudHash(p + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(cloudHash(p + float3(0, 0, 1)), cloudHash(p + float3(1, 0, 1)), f.x),
                     lerp(cloudHash(p + float3(0, 1, 1)), cloudHash(p + float3(1, 1, 1)), f.x), f.y), f.z);
}

float cloudFbm(float3 p) {
    float f = 0.0f;
    float weight = 0.5f;
    [unroll]
    for (int i = 0; i < 5; i++) {
        f += weight * cloudNoise(p);
        p *= 2.0f;
        weight *= 0.5f;
    }
    return f;
}

float GetCloudShadow(float3 worldPos, float3 lightDir, float time, float cloudDensity) {
    if (cloudDensity <= 0.001f || lightDir.y >= -0.01f) return 1.0f;
    
    float3 sunRay = normalize(-lightDir);
    if (sunRay.y <= 0.01f) return 1.0f;
    
    // Cloud layer altitude bounds [150.0m, 450.0m]
    float tBottom = (150.0f - worldPos.y) / sunRay.y;
    float tTop    = (450.0f - worldPos.y) / sunRay.y;
    
    float tEntry = max(tBottom, 0.0f);
    float tExit  = max(tTop, 0.0f);
    if (tExit <= tEntry) return 1.0f;
    
    float marchDist = tExit - tEntry;
    const int NUM_STEPS = 4;
    float stepSize = marchDist / float(NUM_STEPS);
    float opticalDepth = 0.0f;
    
    [unroll]
    for (int i = 0; i < NUM_STEPS; i++) {
        float t = tEntry + (float(i) + 0.5f) * stepSize;
        float3 p = worldPos + sunRay * t;
        
        float h = saturate((p.y - 150.0f) / 300.0f);
        float heightFade = smoothstep(0.0f, 0.3f, h) * smoothstep(1.0f, 0.5f, h);
        
        float3 sampleP = p;
        sampleP.x += time * 1.5f;
        
        float n = cloudFbm(sampleP * 0.005f);
        float coverage = 0.48f;
        float d = saturate((n - coverage) * 5.0f) * heightFade * cloudDensity * 2.0f;
        
        opticalDepth += d * (stepSize * 0.025f);
    }
    
    // Beer-Lambert law for volumetric shadow attenuation
    return saturate(exp(-opticalDepth * 2.0f));
}

// Henyey-Greenstein Phase Function
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * max(0.001, pow(max(denom, 0.0001), 1.5)));
}

[numthreads(8, 8, 1)]
void InjectLightCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float3 volumeDims = float3(160, 90, 128);
    if (any(dispatchThreadID >= volumeDims)) return;

    float3 uvw = (float3(dispatchThreadID) + 0.5) / volumeDims;
    
    // Convert voxel depth to approximate world position
    float zDist = 0.5 + pow(uvw.z, 2.0) * 100.0;
    float aspect = ScreenRes.x / max(ScreenRes.y, 1.0);
    float3 rayDir = normalize(float3((uvw.x * 2.0 - 1.0) * max(tanHalfFov, 0.1) * aspect, -(uvw.y * 2.0 - 1.0) * max(tanHalfFov, 0.1), 1.0));
    float3 worldPos = CameraPos + rayDir * zDist;

    float lightIntensity = dot(LightColor, float3(0.333f, 0.333f, 0.333f));
    if (lightIntensity <= 0.0001f) {
        g_FogVolume[dispatchThreadID] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Direct Lighting with Cloud Shadows
    float3 L = normalize(-LightDir);
    float3 V = normalize(worldPos - CameraPos);
    float cosTheta = dot(L, V);
    
    float phase = PhaseHG(cosTheta, 0.3);
    float cloudShadow = GetCloudShadow(worldPos, LightDir, cloudTime, cloudDensity);
    float3 directLight = LightColor * phase * cloudShadow;

    // Height Falloff
    float fogDensity = 0.02;
    float heightFalloff = 0.05;
    float density = fogDensity * exp(-clamp(worldPos.y * heightFalloff, -20.0, 20.0));
    density = clamp(density, 0.0, 1.0);

    float3 ambient = LightColor * float3(0.05f, 0.08f, 0.12f) * 0.3f;
    float3 finalScattering = (directLight + ambient) * density;
    float extinction = density;

    g_FogVolume[dispatchThreadID] = float4(finalScattering, extinction);
}

[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float3 volumeDims = float3(160, 90, 128);
    if (any(dispatchThreadID.xy >= volumeDims.xy)) return;

    float3 accumulatedScattering = 0;
    float accumulatedTransmittance = 1.0;

    for (uint z = 0; z < 128; ++z)
    {
        uint3 id = uint3(dispatchThreadID.xy, z);
        float4 sliceData = g_FogVolume[id];
        
        float3 voxelScattering = sliceData.rgb;
        float voxelExtinction = sliceData.a;
        
        float stepSize = 0.5;
        float transmittance = exp(-clamp(voxelExtinction * stepSize, 0.0, 20.0));
        
        float3 safeScattering = (voxelScattering - voxelScattering * transmittance) / max(voxelExtinction, 0.0001);
        accumulatedScattering += accumulatedTransmittance * safeScattering;
        accumulatedTransmittance *= transmittance;

        g_AccumulationVolume[id] = float4(accumulatedScattering, accumulatedTransmittance);
    }
}
