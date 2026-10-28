#include "lighting_types.hlsl"
#include "pbr_common.hlsl"
#include "environment_common.hlsl"

struct MaterialData {
    uint albedoIndex;
    uint normalIndex;
    uint rmaIndex;
    uint aoIndex;
    uint emissiveIndex;
    uint indexFormat;
    uint startIndexLocation;
    int baseVertexLocation;
    float3 albedo;
    float roughness;
    float metallic;
    float reflectance;
    float emission;
    float ior;
    float2 uvTiling;
    float2 pad;
};

struct RayPayload
{
    float4 color;
    uint recursionDepth;
    float3 hitPos;
    float3 hitNormal;
    float3 hitAlbedo;
    float hitRoughness;
    float hitMetallic;
    float hitReflectance;
    float hitEmission;
    bool isHit;
    float specularHitDistance;
};

struct ShadowPayload
{
    bool hit;
};

struct RaytracingConstants {
    float4 cameraPos;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 lightDir;
    float4 lightColor;
    int shadowsEnabled;
    int reflectionsEnabled;
    int pathTracingEnabled;
    int maxBounces;
    int frameIndex;
    int samplesPerPixel;
    float cloudDensity;
    float cloudTime;
    float rainWetness;
    float rainPuddles;
    float pad1;
    float pad2;
    uint numPointLights;
    uint numSpotLights;
    float2 lightPadding;
    PointLightData pointLights[16];
    SpotLightData spotLights[16];
    row_major float4x4 giPreviousViewProjection;
    float4 giPreviousCamera;
};

struct Vertex {
    float3 position;
    float3 normal;
    float4 color;
};

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> RenderTarget : register(u0);
RWTexture2D<float> SpecularHitDistance : register(u1);
RWTexture2D<float4> DiffuseGI : register(u2);
ConstantBuffer<RaytracingConstants> Constants : register(b0);
StructuredBuffer<Vertex> Vertices : register(t1);
ByteAddressBuffer Indices : register(t2);
StructuredBuffer<MaterialData> m_materials : register(t3);
StructuredBuffer<Vertex> DynamicVertices[2048] : register(t0, space2);
ByteAddressBuffer DynamicIndices[2048] : register(t0, space3);
Texture2D Textures[] : register(t4);
SamplerState Sampler : register(s0);
// Astra Surface Cache (space 1, matching root signature param 8)
Texture2D<float4> GIHistory : register(t13, space1);
Texture2D<float4> g_AstraAlbedo    : register(t10, space1);
Texture2D<float4> g_AstraNormal    : register(t11, space1);
Texture2D<float4> g_AstraIrradiance: register(t12, space1);

float3 GetSkyColor(float3 dir);

uint Hash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float RandomFloat(inout uint seed) {
    seed = Hash(seed);
    return float(seed & 0x00FFFFFF) / float(0x01000000);
}

float3 SampleCosineHemisphere(float3 N, inout uint seed) {
    float r1 = RandomFloat(seed);
    float r2 = RandomFloat(seed);
    float phi = 2.0f * 3.14159265f * r1;
    float cosTheta = sqrt(max(1.0f - r2, 0.0f));
    float sinTheta = sqrt(max(r2, 0.0f));
    
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    
    float3 up = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float3 GetSkyColor(float3 dir){return EnvironmentSky(dir,Constants.lightDir.xyz,Constants.lightColor.rgb,true);}

#include "cloud_common.hlsl"

float3 EvaluateLocalLights(float3 position, float3 normal, float3 view, float3 albedo, float roughness, float metallic,float reflectance) {
    float3 result=0;
    float3 f0=lerp(0.04f.xxx,albedo,metallic);
    uint points=min(Constants.numPointLights,16u), spots=min(Constants.numSpotLights,16u);
    [loop] for(uint i=0;i<points+spots;++i) {
        float3 location,color;float intensity,range,cone=1;
        if(i<points) {
            PointLightData light=Constants.pointLights[i];location=light.position;color=light.color;intensity=light.intensity;range=light.range;
        } else {
            SpotLightData light=Constants.spotLights[i-points];location=light.position;color=light.color;intensity=light.intensity;range=light.range;
            float3 toward=normalize(position-location);
            float3 axis=light.direction*rsqrt(max(dot(light.direction,light.direction),.00001f));
            float cosine=dot(toward,axis);
            cone=saturate((cosine-light.outerConeCos)/max(light.innerConeCos-light.outerConeCos,0.0001f));cone*=cone;
        }
        if(intensity<=0.0001f || range<=0.01f || cone<=0.0001f)continue;
        float3 vector=location-position;float distance=length(vector);
        if(distance>=range || distance<.001f)continue;
        float3 direction=vector/distance;float nl=saturate(dot(normal,direction));
        if(nl<=0)continue;
        if(Constants.shadowsEnabled) {
            RayDesc ray;ray.Origin=position+normal*.01f;ray.Direction=direction;ray.TMin=.001f;ray.TMax=max(distance-.02f,.002f);
            ShadowPayload shadow;shadow.hit=true;
            TraceRay(Scene,RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_SKIP_CLOSEST_HIT_SHADER|RAY_FLAG_FORCE_OPAQUE,0xFF,0,0,1,ray,shadow);
            if(shadow.hit)continue;
        }
        float ratio=distance/range;float fade=saturate(1-pow(ratio,4));float attenuation=fade*fade/(distance*distance+1);
        result+=EvaluateDirectBRDF(normal,view,direction,albedo,roughness,metallic,reflectance)*color*(intensity*attenuation*cone);
    }
    return result;
}

[shader("raygeneration")]
void RayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float2 dims = float2(DispatchRaysDimensions().xy);
    if (launchIndex.x >= (uint)dims.x || launchIndex.y >= (uint)dims.y) return;
    DiffuseGI[launchIndex]=0;

    float2 xy = launchIndex.xy + 0.5f;
    float2 uv = xy / dims * 2.0f - 1.0f;
    uv.y = -uv.y;

    float3 rayOrigin = Constants.cameraPos.xyz;
    float3 rayDir = normalize(Constants.cameraForward.xyz + uv.x * Constants.cameraRight.xyz + uv.y * Constants.cameraUp.xyz);

    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDir;
    ray.TMin = 0.1f;
    ray.TMax = 2000.0f;

    // PATH TRACING MODE
    if (Constants.pathTracingEnabled != 0)
    {
        uint seed = (launchIndex.x + launchIndex.y * (uint)dims.x) ^ Hash((uint)Constants.frameIndex);
        float3 throughput = float3(1.0f, 1.0f, 1.0f);
        float3 radiance = float3(0.0f, 0.0f, 0.0f);
        
        RayDesc curRay = ray;
        int bounces = clamp(Constants.maxBounces, 1, 4);

        float firstSpecularDistance=0.0f;
        bool firstBounceSpecular=false;
        for (int bounce = 0; bounce <= bounces; bounce++)
        {
            RayPayload hitPayload;
            hitPayload.color = float4(0,0,0,0);
            hitPayload.recursionDepth = bounce;
            hitPayload.isHit = false;

            TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, curRay, hitPayload);

            if (!hitPayload.isHit)
            {
                radiance += throughput * EnvironmentSky(curRay.Direction,Constants.lightDir.xyz,Constants.lightColor.rgb,bounce==0);
                break;
            }
            if(bounce==1&&firstBounceSpecular)firstSpecularDistance=length(hitPayload.hitPos-curRay.Origin);

            // Direct light from sun with shadow ray
            float3 sunDir = normalize(-Constants.lightDir.xyz);
            RayDesc sunRay;
            sunRay.Origin = hitPayload.hitPos + hitPayload.hitNormal * 0.02f;
            sunRay.Direction = sunDir;
            sunRay.TMin = 0.01f;
            sunRay.TMax = 1000.0f;
            ShadowPayload sPayload;
            sPayload.hit = true;
            TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 1, sunRay, sPayload);
            
            float NdotL = max(dot(hitPayload.hitNormal, sunDir), 0.0f);
            if (!sPayload.hit)
            {
                float cloudShadow = GetCloudShadow(hitPayload.hitPos, Constants.lightDir.xyz, Constants.cloudTime, Constants.cloudDensity);
                radiance += throughput * EvaluateDirectBRDF(hitPayload.hitNormal,-curRay.Direction,sunDir,hitPayload.hitAlbedo,hitPayload.hitRoughness,hitPayload.hitMetallic,hitPayload.hitReflectance)*Constants.lightColor.rgb*cloudShadow;
            }

            radiance += throughput * EvaluateLocalLights(hitPayload.hitPos,hitPayload.hitNormal,-curRay.Direction,
                hitPayload.hitAlbedo,hitPayload.hitRoughness,hitPayload.hitMetallic,hitPayload.hitReflectance);
            // Surface emission
            radiance += throughput * hitPayload.color.rgb;

            // Russian Roulette
            if (bounce >= 1)
            {
                float p = clamp(max(throughput.r, max(throughput.g, throughput.b)),.05f,.95f);
                if (RandomFloat(seed) > p || p < 0.001f) break;
                throughput /= max(p, 0.01f);
            }

            // Next ray: Diffuse or Specular
            bool isSpec = (RandomFloat(seed) < hitPayload.hitMetallic || RandomFloat(seed) < (1.0f - hitPayload.hitRoughness) * hitPayload.hitReflectance);
            if(bounce==0)firstBounceSpecular=isSpec;
            float3 nextDir;
            if (isSpec)
            {
                float3 refDir = reflect(curRay.Direction, hitPayload.hitNormal);
                float3 randH = SampleCosineHemisphere(refDir, seed);
                nextDir = normalize(lerp(refDir, randH, hitPayload.hitRoughness * 0.35f));
                throughput *= lerp(float3(1,1,1), hitPayload.hitAlbedo, hitPayload.hitMetallic);
            }
            else
            {
                nextDir = SampleCosineHemisphere(hitPayload.hitNormal, seed);
                throughput *= hitPayload.hitAlbedo;
            }

            curRay.Origin = hitPayload.hitPos + hitPayload.hitNormal * 0.02f;
            curRay.Direction = nextDir;
            curRay.TMin = 0.01f;
            curRay.TMax = 2000.0f;
        }

        RenderTarget[launchIndex] = float4(clamp(radiance, 0.0f, 16.0f), 1.0f);
        SpecularHitDistance[launchIndex]=firstSpecularDistance;
        return;
    }

    // HYBRID DXR RAYTRACING MODE
    RayPayload payload;
    payload.color = float4(0, 0, 0, 0);
    payload.recursionDepth = 0;
    payload.isHit = false;

    TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, ray, payload);
    if (!payload.isHit) {
        payload.color = float4(0, 0, 0, 0);
    } else {
        payload.color = float4(clamp(payload.color.rgb, 0.0f, 16.0f), 1.0f);
    }
    RenderTarget[launchIndex] = payload.color;
    SpecularHitDistance[launchIndex]=payload.isHit?payload.specularHitDistance:0.0f;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    payload.isHit = false;
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.hit = false;
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.specularHitDistance=0.0f;
    uint primitiveIndex = PrimitiveIndex();
    uint instId = InstanceID();
    MaterialData md = m_materials[instId];
    
    uint i0 = 0, i1 = 0, i2 = 0;
    if (md.indexFormat == 0) // 16-bit
    {
        uint byteOffset = md.startIndexLocation * 2 + primitiveIndex * 6;
        uint dwordOffset = byteOffset & ~3;
        uint bitOffset = (byteOffset & 3) * 8;
        
        uint low = Indices.Load(dwordOffset);
        uint high = Indices.Load(dwordOffset + 4);
        
        if (bitOffset == 0)
        {
            i0 = low & 0xFFFF;
            i1 = (low >> 16) & 0xFFFF;
            i2 = high & 0xFFFF;
        }
        else // bitOffset == 16
        {
            i0 = (low >> 16) & 0xFFFF;
            i1 = high & 0xFFFF;
            i2 = (high >> 16) & 0xFFFF;
        }
    }
    else // 32-bit
    {
        uint indexStart = md.startIndexLocation + primitiveIndex * 3;
        if (md.pad.y > 0.5f) {
            uint dynamicIndex=NonUniformResourceIndex((uint)md.pad.y-1);
            i0 = DynamicIndices[dynamicIndex].Load(indexStart * 4 + 0);
            i1 = DynamicIndices[dynamicIndex].Load(indexStart * 4 + 4);
            i2 = DynamicIndices[dynamicIndex].Load(indexStart * 4 + 8);
        } else {
            i0 = Indices.Load(indexStart * 4 + 0);
            i1 = Indices.Load(indexStart * 4 + 4);
            i2 = Indices.Load(indexStart * 4 + 8);
        }
    }

    uint v0Idx = md.baseVertexLocation + i0;
    uint v1Idx = md.baseVertexLocation + i1;
    uint v2Idx = md.baseVertexLocation + i2;

    Vertex v0, v1, v2;
    if (md.pad.y > 0.5f) {
        uint dynamicIndex=NonUniformResourceIndex((uint)md.pad.y-1);
        v0=DynamicVertices[dynamicIndex][v0Idx];
        v1=DynamicVertices[dynamicIndex][v1Idx];
        v2=DynamicVertices[dynamicIndex][v2Idx];
    } else {
        v0=Vertices[v0Idx];
        v1=Vertices[v1Idx];
        v2=Vertices[v2Idx];
    }

    float3 barycentrics = float3(1.f - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    float3 normal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z;
    float3 hitPos = v0.position * barycentrics.x + v1.position * barycentrics.y + v2.position * barycentrics.z;
    normal = normalize(mul(normal, (float3x3)WorldToObject3x4()));

    // UV generation from vertex data
    float2 uv0 = float2(v0.color.z, v0.color.w);
    float2 uv1 = float2(v1.color.z, v1.color.w);
    float2 uv2 = float2(v2.color.z, v2.color.w);
    if (md.pad.x < 0.5f) {
        uv0 = ProjectedMeshUV(v0.position, v0.normal);
        uv1 = ProjectedMeshUV(v1.position, v1.normal);
        uv2 = ProjectedMeshUV(v2.position, v2.normal);
    }
    float2 uv = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
    float2 uvTiling = md.uvTiling;
    if (uvTiling.x != 0.0f || uvTiling.y != 0.0f) {
        uv *= uvTiling;
    }
    
    // Albedo & textures
    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (md.albedoIndex >= 1 && md.albedoIndex < 1000) {
        texColor = Textures[NonUniformResourceIndex(md.albedoIndex)].SampleLevel(Sampler, uv, 0);
    }
    float3 vertColor = float3(1,1,1);
    // Only apply vertex color if it's not black (i.e. the mesh actually has per-vertex colors)
    float vertColorLum = dot(vertColor, float3(0.299f, 0.587f, 0.114f));
    float3 albedo = md.albedo * texColor.rgb * (vertColorLum > 0.001f ? vertColor : float3(1.0f, 1.0f, 1.0f));

    // Normal map
    if (md.normalIndex >= 1 && md.normalIndex < 1000) {
        float3 mapN = Textures[NonUniformResourceIndex(md.normalIndex)].SampleLevel(Sampler, uv, 0).rgb * 2.0 - 1.0;
        float3 N = normal;
        float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
        float3 T = normalize(cross(up, N));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        normal = normalize(mul(mapN, TBN));
    }

    // Roughness & Metallic
    float roughness = md.roughness;
    float metallic = md.metallic;
    float ao = 1.0f;
    if (md.rmaIndex >= 1 && md.rmaIndex < 1000) {
        float3 rma = Textures[NonUniformResourceIndex(md.rmaIndex)].SampleLevel(Sampler, uv, 0).rgb;
        roughness *= rma.r;
        metallic *= rma.g;
        ao *= rma.b;
    }
    if (md.aoIndex >= 1 && md.aoIndex < 1000) {
        ao *= Textures[NonUniformResourceIndex(md.aoIndex)].SampleLevel(Sampler, uv, 0).r;
    }
    roughness = clamp(roughness, 0.015f, 1.0f);
    metallic = clamp(metallic, 0.0f, 1.0f);

    float3 worldPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

    // Rain Wetness, Vertical Water Flow Streams & Puddles in Ray Tracing
    float upwardExposure = saturate(normal.y + 0.35f);
    if (Constants.rainWetness > 0.001f && upwardExposure > 0.01f) {
        albedo *= lerp(1.0f, 0.58f, Constants.rainWetness * upwardExposure);
    }

    // 1. Realistic Multi-Octave Puddle & Rain Impact Ripples on Horizontal Surfaces
    float puddleMask = 0.0f;
    if (Constants.rainPuddles > 0.001f && normal.y > 0.40f) {
        puddleMask = saturate((normal.y - 0.40f) * 2.5f) * Constants.rainPuddles;
        if (puddleMask > 0.01f) {
            float t = Constants.cloudTime * 3.6f;
            float2 totalRippleNorm = float2(0.0f, 0.0f);
            
            // Octave 1: Primary rain drop impact ripples
            {
                float2 p1 = worldPos.xz * 5.2f;
                float2 cell1 = floor(p1);
                float2 f1 = frac(p1);
                float h1 = sin(dot(cell1, float2(12.9898f, 78.233f))) * 43758.5453f;
                float2 jitter1 = float2(sin(h1), cos(h1 * 1.37f)) * 0.32f;
                float2 dVec1 = f1 - (0.5f + jitter1);
                float dist1 = length(dVec1);
                float dropTime1 = frac(t * 1.1f + frac(h1));
                float rWave1 = 0.04f + 0.46f * dropTime1;
                float delta1 = dist1 - rWave1;
                float wave1 = sin(delta1 * 40.0f) * exp(-delta1 * delta1 * 140.0f) * pow(saturate(1.0f - dropTime1), 1.5f);
                totalRippleNorm += (dVec1 / max(dist1, 0.001f)) * wave1 * 0.26f;
            }

            // Octave 2: Secondary offset ripples
            {
                float2 p2 = worldPos.xz * 7.4f + float2(17.3f, 31.7f);
                float2 cell2 = floor(p2);
                float2 f2 = frac(p2);
                float h2 = sin(dot(cell2, float2(39.346f, 11.135f))) * 43758.5453f;
                float2 jitter2 = float2(sin(h2 * 1.21f), cos(h2)) * 0.32f;
                float2 dVec2 = f2 - (0.5f + jitter2);
                float dist2 = length(dVec2);
                float dropTime2 = frac(t * 1.45f + frac(h2));
                float rWave2 = 0.03f + 0.44f * dropTime2;
                float delta2 = dist2 - rWave2;
                float wave2 = sin(delta2 * 45.0f) * exp(-delta2 * delta2 * 160.0f) * pow(saturate(1.0f - dropTime2), 1.5f);
                totalRippleNorm += (dVec2 / max(dist2, 0.001f)) * wave2 * 0.20f;
            }

            // Wind wave
            float windWave = sin(dot(worldPos.xz, float2(4.5f, 6.2f)) + t * 2.2f) * 0.06f +
                             cos(dot(worldPos.xz, float2(-6.1f, 4.3f)) - t * 1.8f) * 0.06f;
            
            normal = normalize(normal + float3(totalRippleNorm.x + windWave, 0.0f, totalRippleNorm.y + windWave) * puddleMask);
        }
    }

    // Dynamic Wetness & Puddle Roughness Smoothing
    if (Constants.rainWetness > 0.001f && upwardExposure > 0.05f) {
        roughness = lerp(roughness, max(roughness * 0.40f, 0.15f), Constants.rainWetness * upwardExposure * 0.65f);
    }
    if (puddleMask > 0.01f) {
        roughness = lerp(roughness, 0.06f, puddleMask * 0.85f);
    }

    float3 lightDir = normalize(-Constants.lightDir.xyz);

    if(dot(normal,WorldRayDirection())>0)normal=-normal;
    float3 emission=albedo*md.emission;
    if(md.emissiveIndex>=1&&md.emissiveIndex<1000)emission+=Textures[NonUniformResourceIndex(md.emissiveIndex)].SampleLevel(Sampler,uv,0).rgb*max(md.emission,1.f);
    payload.isHit=true;payload.hitPos=worldPos;payload.hitNormal=normal;payload.hitAlbedo=albedo;
    payload.hitRoughness=roughness;payload.hitMetallic=metallic;payload.hitReflectance=md.reflectance;payload.hitEmission=md.emission;
    payload.specularHitDistance=0;
    // The iterative path tracer evaluates lighting in RayGen, not recursively here.
    if(Constants.pathTracingEnabled){payload.color=float4(emission,1);return;}

    // Physical Area Light Sun Soft Shadows (DXR Raytracing with Contact Hardening)
    float shadowFactor = 1.0f;
    if (Constants.shadowsEnabled != 0) {
        // Orthonormal tangent basis around sun lightDir
        float3 up = (abs(lightDir.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 lightTangent = normalize(cross(up, lightDir));
        float3 lightBitangent = cross(lightDir, lightTangent);

        // Sub-pixel screen hash for dithered stratified cone sampling
        uint2 pixelCoord = uint2(DispatchRaysIndex().xy);
        float seed = frac(sin(dot(float2(pixelCoord), float2(12.9898f, 78.233f))) * 43758.5453f);
        
        float sunAngularRadius = 0.00465f;
        float hitSum = 0.0f;
        int shadowSamples = (payload.recursionDepth == 0) ? 4 : 1;
        
        [unroll]
        for (int s = 0; s < shadowSamples; ++s) {
            float angle = (float(s) + seed) * (6.2831853f / float(shadowSamples));
            float radius = sqrt((float(s) + 0.5f) / float(shadowSamples)) * sunAngularRadius;
            float2 diskOffset = float2(cos(angle), sin(angle)) * radius;

            float3 jitteredLightDir = normalize(lightDir + lightTangent * diskOffset.x + lightBitangent * diskOffset.y);

            RayDesc shadowRay;
            shadowRay.Origin = worldPos + normal * 0.001f;
            shadowRay.Direction = (payload.recursionDepth == 0) ? jitteredLightDir : lightDir;
            shadowRay.TMin = 0.001f;
            shadowRay.TMax = 1000.0f;

            ShadowPayload shadowPayload;
            shadowPayload.hit = true;

            TraceRay(Scene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 1, shadowRay, shadowPayload);
            hitSum += shadowPayload.hit ? 0.0f : 1.0f;
        }
        shadowFactor = hitSum / float(shadowSamples);
    }

    // Diffuse & Specular (Blinn-Phong / PBR approximation with Cloud Shadows)
    float cloudShadow = GetCloudShadow(worldPos, Constants.lightDir.xyz, Constants.cloudTime, Constants.cloudDensity);
    float totalShadow = shadowFactor * cloudShadow;

    float diff = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = diff * Constants.lightColor.rgb * totalShadow;
    
    float3 viewDir = normalize(-WorldRayDirection());
    float3 halfDir = normalize(lightDir + viewDir);
    float specAngle = max(dot(normal, halfDir), 0.0f);
    float specPower = lerp(8.0f, 256.0f, 1.0f - roughness);
    float spec = pow(specAngle, specPower) * (1.0f - roughness);
    float3 specular = spec * Constants.lightColor.rgb * totalShadow * md.reflectance;

    // Physical Hemisphere Ambient derived directly from Sky irradiance
    float hemi = saturate(normal.y * 0.5f + 0.5f);
    float3 skyZenith = GetSkyColor(float3(0.0f, 1.0f, 0.0f));
    float3 groundBounce = GetSkyColor(float3(0.0f, -1.0f, 0.0f)) * 0.35f;
    float3 hemiAmbient = lerp(groundBounce, skyZenith, hemi) * 0.45f;
    float ambientCloudMod = lerp(0.70f, 1.0f, cloudShadow);
    float3 f0=MaterialF0(albedo,metallic,md.reflectance);
    float3 diffuseAlbedo=albedo*(1-metallic)*(1-f0);
    float3 ambient=DiffuseEnvironment(normal,diffuseAlbedo,Constants.lightDir.xyz,Constants.lightColor.rgb)*ao*ambientCloudMod;
    // Terminal GI rays return direct lighting only: no unoccluded ambient leaks through walls.
    if(payload.recursionDepth==3)ambient=0;
    if(payload.recursionDepth==0&&Constants.pad1>0){
        uint seed=Hash(DispatchRaysIndex().x+DispatchRaysIndex().y*DispatchRaysDimensions().x)^Hash(uint(Constants.frameIndex));
        float3 incident=0;
        // Stratified cosine samples cover the hemisphere without random clumps.
        // Keep the pattern stable in world lighting when temporal AA/RR is disabled.
        const uint giSamples=8;
        float rotation=RandomFloat(seed);
        float3 up=abs(normal.z)<.999f?float3(0,0,1):float3(1,0,0);
        float3 tangent=normalize(cross(up,normal));float3 bitangent=cross(normal,tangent);
        [unroll]for(uint sample=0;sample<giSamples;++sample){
            float radial=(float(sample)+.5f)/float(giSamples);
            float angle=2*PI*frac(rotation+float(sample)*.61803398875f);
            float3 direction=normalize(tangent*(sqrt(radial)*cos(angle))+bitangent*(sqrt(radial)*sin(angle))+normal*sqrt(1-radial));
            RayDesc giRay;giRay.Origin=worldPos+normal*.01f;giRay.Direction=direction;giRay.TMin=.002f;giRay.TMax=2000;
            RayPayload gi;gi.color=0;gi.recursionDepth=3;gi.isHit=false;
            TraceRay(Scene,RAY_FLAG_FORCE_OPAQUE,0xFF,0,0,0,giRay,gi);
            incident+=gi.isHit?gi.color.rgb:EnvironmentSky(direction,Constants.lightDir.xyz,Constants.lightColor.rgb,false);
        }
        ambient=diffuseAlbedo*(incident/float(giSamples))*ao*Constants.pad1;
        if(Constants.pad2>.5f){
            float4 previousClip=mul(float4(worldPos,1),Constants.giPreviousViewProjection);
            float2 previousUV=previousClip.xy/max(previousClip.w,.0001f)*float2(.5f,-.5f)+.5f;
            if(previousClip.w>0 && all(previousUV>=0) && all(previousUV<1)){
                int2 previousPixel=int2(previousUV*float2(DispatchRaysDimensions().xy));
                float4 history=GIHistory.Load(int3(previousPixel,0));
                float expectedDistance=length(worldPos-Constants.giPreviousCamera.xyz);
                if(history.a>0 && abs(history.a-expectedDistance)<max(.025f,expectedDistance*.003f) && all(isfinite(history)))
                    ambient=lerp(ambient,history.rgb,.94f);
            }
        }
        DiffuseGI[DispatchRaysIndex().xy]=float4(min(ambient,16.f),RayTCurrent());
        ambient=0;
    }

    // Raytraced Reflection with Physical PBR Roughness & Fresnel Falloff
    float3 reflectColor = float3(0, 0, 0);
    uint maxReflBounces = uint(clamp(Constants.maxBounces, 1, 2));
    if (Constants.reflectionsEnabled != 0 && payload.recursionDepth < maxReflBounces && roughness < .95f) {
        RayDesc reflectRay;
        reflectRay.Origin = worldPos + normal * 0.002f;
        float3 baseReflectDir = reflect(WorldRayDirection(), normal);
        
        reflectRay.Direction = normalize(baseReflectDir);
        reflectRay.TMin = 0.01f;
        reflectRay.TMax = 1000.0f;

        RayPayload reflectPayload;
        reflectPayload.color = float4(0, 0, 0, 0);
        reflectPayload.recursionDepth = payload.recursionDepth + 1;

        TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xFF, 0, 0, 0, reflectRay, reflectPayload);
        if(reflectPayload.isHit)payload.specularHitDistance=length(reflectPayload.hitPos-worldPos);
        
        if (!reflectPayload.isHit) {
            reflectPayload.color = float4(EnvironmentSky(reflectRay.Direction,Constants.lightDir.xyz,Constants.lightColor.rgb,false) * 0.5f, 1.0f);
        }

        // Schlick Fresnel & Physical Quadratic Roughness Decay (eliminates artificial glare on rough/shadowed surfaces)
        float3 F0 = f0;
        float cosTheta = saturate(dot(normal, viewDir));
        float3 F_env = F0 + (max(1.0f - roughness, F0) - F0) * pow(1.0f - cosTheta, 5.0f);
        
        float specRoughnessFactor = (1.0f - roughness) * (1.0f - roughness);
        float specOcclusion = 1.0f;
        float reflectStrength = specRoughnessFactor;
        reflectColor = clamp(reflectPayload.color.rgb, 0.0f, 4.0f) * F_env * reflectStrength;
    }

    float3 baseColor = EvaluateDirectBRDF(normal,viewDir,lightDir,albedo,roughness,metallic,md.reflectance)*Constants.lightColor.rgb*totalShadow+ambient;
    float3 outCol = baseColor + reflectColor + emission;
    if(!Constants.pathTracingEnabled) outCol+=EvaluateLocalLights(worldPos,normal,viewDir,albedo,roughness,metallic,md.reflectance);
    if (!all(isfinite(outCol))) outCol = float3(0.0f, 0.0f, 0.0f);
    payload.color = float4(clamp(outCol, 0.0f, 16.0f), 1.0f);

    payload.isHit = true;
    payload.hitPos = worldPos;
    payload.hitNormal = normal;
    payload.hitAlbedo = albedo;
    payload.hitRoughness = roughness;
    payload.hitMetallic = metallic;
    payload.hitReflectance = md.reflectance;
    payload.hitEmission = md.emission;
}
