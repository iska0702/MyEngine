#include "pbr_common.hlsl"
#include "environment_common.hlsl"
#include "lighting_types.hlsl"
cbuffer SceneConstantBuffer : register(b0)
{
    row_major float4x4 ViewProj;
    row_major float4x4 PrevViewProj;
    row_major float4x4 CleanViewProj;
    row_major float4x4 PrevCleanViewProj;
    row_major float4x4 Model;
    row_major float4x4 SunViewProj;
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
    row_major float4x4 PreviousModel;
};

Texture2D    g_Textures[1000] : register(t0, space0);
SamplerState g_Sampler        : register(s0, space0);

// Astra Surface Cache and Volumetric Fog are now grouped with other space1 textures below.
SamplerState g_LinearSampler      : register(s1, space0); // VSM/RVT Linear Sampler
SamplerComparisonState g_ShadowSampler : register(s2, space0);

struct Vertex
{
    float3 position;
    float3 normal;
    float4 color;
};

StructuredBuffer<Vertex> g_Vertices : register(t0, space1);
ByteAddressBuffer        g_Indices  : register(t1, space1);

struct Meshlet {
    uint VertexOffset;
    uint VertexCount;
    uint PrimitiveOffset;
    uint PrimitiveCount;
    float Center[3];
    float Radius;
    float LODError;       // Geometric error of this cluster
    float MaxParentError; // Error of the parent cluster
    uint ParentID;        // Index of parent cluster in DAG
    float3 ConeNormal;    // Average normal vector
    float ConeCutoff;     // cos(threshold_angle)
};
StructuredBuffer<Meshlet> g_Meshlets : register(t2, space1);
Texture2D<float> g_HiZ : register(t3, space1);
Texture2D<float> g_vsmAtlas : register(t4, space1);
Texture2D<uint>  g_vsmPageTable : register(t5, space1);
Texture2D<float4> g_rvtAlbedoAtlas : register(t6, space1);
Texture2D<float4> g_rvtNormalAtlas : register(t7, space1);
Texture2D<float4> g_rvtIrradianceAtlas : register(t8, space1);
Texture2D<uint>   g_rvtPageTable : register(t9, space1);
Texture3D         g_volumetricFogVolume : register(t10, space1);

struct Payload {
    uint MeshletIndices[32];
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD; // Derived from position to map textures
    float4 curPos : TEXCOORD1;
    float4 prevPos : TEXCOORD2;
};

groupshared uint s_Count;
groupshared uint s_Indices[32];

bool VisibleMeshlet(Meshlet m){
    float3 center=float3(m.Center[0],m.Center[1],m.Center[2]);
    if(!all(isfinite(center))||!isfinite(m.Radius)||m.Radius<0)return true;
    // Transform clip planes into object space. This remains conservative under
    // non-uniform/negative scale and avoids an approximate world-space radius.
    float4x4 clip=mul(Model,ViewProj);
    float4 x=float4(clip[0][0],clip[1][0],clip[2][0],clip[3][0]);
    float4 y=float4(clip[0][1],clip[1][1],clip[2][1],clip[3][1]);
    float4 z=float4(clip[0][2],clip[1][2],clip[2][2],clip[3][2]);
    float4 w=float4(clip[0][3],clip[1][3],clip[2][3],clip[3][3]);
    float4 planes[6]={w+x,w-x,w+y,w-y,z,w-z};
    float radius=m.Radius+max(0.0001,m.Radius*0.0001);
    [unroll]for(uint i=0;i<6;++i)
        if(dot(planes[i].xyz,center)+planes[i].w < -radius*length(planes[i].xyz))return false;
    return true;
}

[NumThreads(32, 1, 1)]
void ASMain(
    uint gtc : SV_GroupThreadID,
    uint gid : SV_GroupID,
    uint dtid : SV_DispatchThreadID
)
{
    if (gtc == 0) s_Count = 0;
    GroupMemoryBarrierWithGroupSync();

    uint meshletIndex = meshletStart + dtid;
    if (dtid < meshletCount)
    {
        if(VisibleMeshlet(g_Meshlets[meshletIndex])){
            uint idx;
            InterlockedAdd(s_Count, 1, idx);
            s_Indices[idx] = meshletIndex;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    Payload p;
    for (uint i = 0; i < s_Count; ++i) p.MeshletIndices[i] = s_Indices[i];
    DispatchMesh(s_Count, 1, 1, p);
}

// Mesh Shader: Process Visible Meshlet
[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void MSMain(
    in payload Payload p,
    uint gtc : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out indices uint3 tris[126],
    out vertices PSInput verts[128] 
)
{
    uint meshletIndex = p.MeshletIndices[gid];
    Meshlet m = g_Meshlets[meshletIndex];

    uint triCount = min(m.PrimitiveCount, 42u);
    uint vertCount = triCount * 3;

    SetMeshOutputCounts(vertCount, triCount);

    if (gtc < triCount)
    {
        uint startIdx = m.PrimitiveOffset + gtc * 3;
        
        uint i0 = g_Indices.Load((startIdx + 0) * 4);
        uint i1 = g_Indices.Load((startIdx + 1) * 4);
        uint i2 = g_Indices.Load((startIdx + 2) * 4);

        uint outV0 = gtc * 3 + 0;
        uint outV1 = gtc * 3 + 1;
        uint outV2 = gtc * 3 + 2;

        tris[gtc] = uint3(outV0, outV1, outV2);

        uint vIndices[3] = { i0, i1, i2 };
        uint outIndices[3] = { outV0, outV1, outV2 };

        [unroll]
        for (uint i = 0; i < 3; i++)
        {
            Vertex v = g_Vertices[baseVertexLocation + vIndices[i]];
            uint outIdx = outIndices[i];

            float4 worldPos = mul(float4(v.position, 1.0f), Model);
            verts[outIdx].position = mul(worldPos, ViewProj);
            verts[outIdx].worldPos = worldPos.xyz;
            float3x3 basis = (float3x3)Model;
            float3x3 cofactors = float3x3(cross(basis[1], basis[2]),
                                        cross(basis[2], basis[0]),
                                        cross(basis[0], basis[1]));
            float determinant = dot(basis[0], cofactors[0]);
            verts[outIdx].normal = normalize(mul(v.normal, cofactors) * (determinant < 0.0f ? -1.0f : 1.0f));
            // Z/W store imported UVs, so they must never tint the material's RGB.
            verts[outIdx].color = float4(1,1,1,1);
            verts[outIdx].uv = padIndices != 0 ? v.color.zw : ProjectedMeshUV(v.position,v.normal);
            verts[outIdx].curPos = mul(worldPos, CleanViewProj);
            verts[outIdx].prevPos = mul(mul(float4(v.position, 1), PreviousModel), PrevCleanViewProj);
        }
    }
}

// PBR variables

float3 GetSkyColor(float3 dir){return EnvironmentSky(dir,LightDir,LightColor,false);}

// Interleaved Gradient Noise for sub-pixel jitter
float InterleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// 16-Tap Poisson Disk Samples (Normalized [-1, 1])
static const float2 PoissonDisk16[16] = {
    float2(-0.94201624, -0.39906216),
    float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870),
    float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),
    float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),
    float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)
};

#include "cloud_common.hlsl"

struct PSOutput {
    float4 color : SV_Target0;
    float4 velocity : SV_Target1;
    float4 normal : SV_Target2;
    float4 diffuseAlbedo : SV_Target3;
    float4 specularAlbedo : SV_Target4;
};

PSOutput PSMain(PSInput input)
{
    PSOutput outVals;

    float2 texUV = input.uv * uvTiling;
    if (all(texUV == 0.0f)) texUV = input.uv;

    float3 albedo = input.color.rgb * matAlbedo;

    if (albedoIndex > 0 && albedoIndex < 1000) {
        albedo *= g_Textures[albedoIndex].Sample(g_Sampler, texUV).rgb;
    }

    float3 N = normalize(input.normal);
    if (normalIndex > 0 && normalIndex < 1000) {
        float3 mapN = g_Textures[normalIndex].Sample(g_Sampler, texUV).rgb * 2.0 - 1.0;
        float3 dp1 = ddx(input.worldPos);
        float3 dp2 = ddy(input.worldPos);
        float2 duv1 = ddx(texUV);
        float2 duv2 = ddy(texUV);
        float3 dp2perp = cross(dp2, N);
        float3 dp1perp = cross(N, dp1);
        float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
        float maxDot = max(dot(T, T), dot(B, B));
        if (maxDot > 0.00001f) {
            float invmax = rsqrt(maxDot);
            float3x3 TBN = float3x3(T * invmax, B * invmax, N);
            N = normalize(mul(mapN, TBN));
        }
    }

    // Rain Wetness, Vertical Water Flow Streams & Puddles on 3D Models
    float upwardExposure = saturate(N.y + 0.35f); // Bottom surfaces (Ny < -0.35) are sheltered from rain
    if (rainWetness > 0.001f && upwardExposure > 0.01f) {
        // Porosity darkening: wet surfaces reflect less diffuse light as water fills micro-cavities
        albedo *= lerp(1.0f, 0.58f, rainWetness * upwardExposure);
    }

    // Rain puddles & surface interaction (no procedural floor plane ripples)
    float puddleMask = 0.0f;
    if (rainPuddles > 0.001f && N.y > 0.40f) {
        puddleMask = saturate((N.y - 0.40f) * 2.5f) * rainPuddles;
    }

    // 2. Animated Gravity Water Flow & Dripping Streams on Vertical Model Faces (|N.y| < 0.75)
    float flowMask = 0.0f;
    if (rainWetness > 0.001f && abs(N.y) < 0.75f && upwardExposure > 0.05f) {
        flowMask = (1.0f - saturate(abs(N.y) * 1.33f)) * rainWetness;
        float streamTime = cloudTime * 2.0f;
        
        // Axis-aligned vertical projection UV (avoids diagonal corner shearing)
        float horizCoord = (abs(N.x) > abs(N.z)) ? input.worldPos.z : input.worldPos.x;
        float2 flowUv = float2(horizCoord * 4.0f, -input.worldPos.y * 3.0f);
        
        // Smooth undulating water film streams
        float streamCell = sin(flowUv.x * 6.0f + sin(flowUv.y * 2.0f) * 1.2f) * 0.5f + 0.5f;
        float streamCore = pow(streamCell, 8.0f);
        float streamFlow = sin(flowUv.y * 4.0f - streamTime * 4.0f);
        
        // Gentle micro-rivulet perturbation (smooth, natural, no harsh dark blotches)
        float totalFlowPerturb = streamCore * streamFlow * 0.10f * flowMask;
        
        // Perturb surface normal tangentially to gravity flow
        float3 flowTangent = cross(N, float3(0.0f, 1.0f, 0.0f));
        if (length(flowTangent) > 0.01f) {
            flowTangent = normalize(flowTangent);
            float3 flowBitangent = cross(N, flowTangent);
            N = normalize(N + flowTangent * totalFlowPerturb + flowBitangent * (totalFlowPerturb * 0.35f));
        }
    }

    float roughness = matRoughness;
    float metallic = matMetallic;
    float ao = 1.0f;

    if (rmaIndex > 0 && rmaIndex < 1000) {
        float3 rma = g_Textures[rmaIndex].Sample(g_Sampler, texUV).rgb;
        roughness *= rma.r;
        metallic *= rma.g;
        ao *= rma.b;
    }
    if (aoIndex > 0 && aoIndex < 1000) {
        ao *= g_Textures[aoIndex].Sample(g_Sampler, texUV).r;
    }

    // Dynamic Wetness & Puddle roughness smoothing (water layer has natural glossy sheen)
    if (rainWetness > 0.001f && upwardExposure > 0.05f) {
        roughness = lerp(roughness, max(roughness * 0.40f, 0.15f), rainWetness * upwardExposure * 0.65f);
    }
    if (flowMask > 0.01f) {
        roughness = lerp(roughness, max(roughness * 0.45f, 0.18f), flowMask * 0.60f);
    }
    if (puddleMask > 0.01f) {
        roughness = lerp(roughness, 0.08f, puddleMask * 0.75f); // Natural wet gloss
    }

    float3 dNdx = ddx(N);
    float3 dNdy = ddy(N);
    float normalVariance = dot(dNdx, dNdx) + dot(dNdy, dNdy);
    float kernelRoughness = clamp(2.0f * normalVariance, 0.0f, 0.5f);
    if (puddleMask > 0.01f || flowMask > 0.01f) {
        kernelRoughness = 0.0f; // Preserve crystal-clear liquid specular glint
    }
    roughness = sqrt(roughness * roughness + kernelRoughness);
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic = clamp(metallic, 0.0f, 1.0f);

    float3 V = normalize(CameraPos - input.worldPos);
    N = normalize(N);
    if (dot(N, V) < 0.0f) N = -N; // Two-sided normal lighting support for cloth & thin geometry
    float3 L = normalize(-LightDir);
    float3 H = normalize(V + L);

    // Cook-Torrance BRDF for Direct Sun Light
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = MaterialF0(albedo,metallic,matReflectance);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.01f) * max(dot(N, L), 0.01f) + 0.001f;
    float3 specular = clamp(numerator / denominator, 0.0f, 4.0f);

    float3 kS = F;
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - metallic);

    float NdotL = max(dot(N, L), 0.0f);

    // 16-Tap Poisson Disk Soft Directional Shadows
    float shadowFactor = 1.0f;
    if (NdotL > 0.0001f && Pad2 > 0.5f) {
        float slopeFactor = saturate(1.0f - NdotL);
        float3 normalOffset = N * (0.012f + slopeFactor * 0.028f);
        float4 shadowClip = mul(float4(input.worldPos + normalOffset, 1.0f), SunViewProj);
        if (shadowClip.w > 0.001f) {
            float3 shadowNDC = shadowClip.xyz / shadowClip.w;
            float2 shadowUV = shadowNDC.xy * float2(0.5f, -0.5f) + 0.5f;
            float currentDepth = shadowNDC.z;

            if (all(shadowUV >= 0.001f) && all(shadowUV <= 0.999f) && currentDepth >= 0.0f && currentDepth <= 1.0f) {
                float bias = max(0.00012f + slopeFactor * 0.00035f, 0.00006f);
                float2 texelSize = float2(1.0f / 2048.0f, 1.0f / 2048.0f);
                float filterRadius = 2.2f;

                // Random sub-pixel rotation angle per screen pixel (eliminates banding)
                float noise = InterleavedGradientNoise(input.position.xy);
                float angle = noise * 6.2831853f;
                float sinA = sin(angle);
                float cosA = cos(angle);
                float2x2 rotMat = float2x2(cosA, -sinA, sinA, cosA);

                // 16-Tap Poisson Disk PCF Filtering
                float shadowSum = 0.0f;
                [unroll]
                for (int i = 0; i < 16; ++i) {
                    float2 sampleOffset = mul(PoissonDisk16[i], rotMat) * texelSize * filterRadius;
                    shadowSum += g_vsmAtlas.SampleCmpLevelZero(g_ShadowSampler, shadowUV + sampleOffset, currentDepth - bias);
                }
                shadowFactor = shadowSum / 16.0f;

                // Smoothly fade shadows at the outer boundary of the 120m frustum
                float edgeDist = min(min(shadowUV.x, 1.0f - shadowUV.x), min(shadowUV.y, 1.0f - shadowUV.y));
                float edgeFade = saturate(edgeDist * 15.0f);
                shadowFactor = lerp(1.0f, shadowFactor, edgeFade);
            }
        }
    }

    float cloudShadow = GetCloudShadow(input.worldPos, LightDir, cloudTime, cloudDensity);
    float3 directLight = (kD * albedo / PI + specular) * LightColor * (NdotL * shadowFactor * cloudShadow);

    // --- Local Point Lights PBR Illumination ---
    [loop]
    for (uint plIdx = 0; plIdx < numPointLights; ++plIdx) {
        PointLightData pl = pointLights[plIdx];
        if (pl.intensity <= 0.0001f || pl.range <= 0.01f) continue;

        float3 lightVec = pl.position - input.worldPos;
        float d = length(lightVec);
        if (d < pl.range) {
            float3 L_point = lightVec / max(d, 0.0001f);
            float distRatio = d / pl.range;
            float num = saturate(1.0f - distRatio * distRatio * distRatio * distRatio);
            float atten = (num * num) / (d * d + 1.0f);

            float3 H_point = normalize(V + L_point);
            float NDF_p = DistributionGGX(N, H_point, roughness);
            float G_p = GeometrySmith(N, V, L_point, roughness);
            float3 F_p = fresnelSchlick(max(dot(H_point, V), 0.0f), F0);

            float3 specNum_p = NDF_p * G_p * F_p;
            float specDenom_p = 4.0f * max(dot(N, V), 0.01f) * max(dot(N, L_point), 0.01f) + 0.001f;
            float3 spec_p = clamp(specNum_p / specDenom_p, 0.0f, 4.0f);

            float3 kS_p = F_p;
            float3 kD_p = (float3(1.0f, 1.0f, 1.0f) - kS_p) * (1.0f - metallic);
            float NdotL_p = max(dot(N, L_point), 0.0f);

            directLight += (kD_p * albedo / PI + spec_p) * pl.color * (pl.intensity * atten * NdotL_p);
        }
    }

    // --- Local Spot Lights PBR Illumination ---
    [loop]
    for (uint slIdx = 0; slIdx < numSpotLights; ++slIdx) {
        SpotLightData sl = spotLights[slIdx];
        if (sl.intensity <= 0.0001f || sl.range <= 0.01f) continue;

        float3 lightVec = sl.position - input.worldPos;
        float d = length(lightVec);
        if (d < sl.range) {
            float3 L_spot = lightVec / max(d, 0.0001f);
            float cosTheta = dot(-L_spot, normalize(sl.direction));
            if (cosTheta > sl.outerConeCos) {
                float distRatio = d / sl.range;
                float num = saturate(1.0f - distRatio * distRatio * distRatio * distRatio);
                float atten = (num * num) / (d * d + 1.0f);

                float spotAtten = saturate((cosTheta - sl.outerConeCos) / max(sl.innerConeCos - sl.outerConeCos, 0.0001f));
                spotAtten = spotAtten * spotAtten;

                float3 H_spot = normalize(V + L_spot);
                float NDF_s = DistributionGGX(N, H_spot, roughness);
                float G_s = GeometrySmith(N, V, L_spot, roughness);
                float3 F_s = fresnelSchlick(max(dot(H_spot, V), 0.0f), F0);

                float3 specNum_s = NDF_s * G_s * F_s;
                float specDenom_s = 4.0f * max(dot(N, V), 0.01f) * max(dot(N, L_spot), 0.01f) + 0.001f;
                float3 spec_s = clamp(specNum_s / specDenom_s, 0.0f, 4.0f);

                float3 kS_s = F_s;
                float3 kD_s = (float3(1.0f, 1.0f, 1.0f) - kS_s) * (1.0f - metallic);
                float NdotL_s = max(dot(N, L_spot), 0.0f);

                directLight += (kD_s * albedo / PI + spec_s) * sl.color * (sl.intensity * atten * spotAtten * NdotL_s);
            }
        }
    }

    // Environmental Fresnel & Soft Ambient Reflection
    float envFresnel = pow(clamp(1.0f - max(dot(N, V), 0.0f), 0.0f, 1.0f), 5.0f);
    float3 F_env = F0 + (max(1.0f - roughness, F0) - F0) * envFresnel;
    float3 R = reflect(-V, N);
    
    // Ambient sky reflection
    float3 rawSky = GetSkyColor(R);
    float3 skyReflection = rawSky * 0.25f;

    // Physical Hemisphere Ambient derived directly from Sky irradiance
    float hemi = saturate(N.y * 0.5f + 0.5f);
    float3 skyZenith = GetSkyColor(float3(0.0f, 1.0f, 0.0f));
    float3 groundBounce = GetSkyColor(float3(0.0f, -1.0f, 0.0f)) * 0.35f;
    float3 hemiAmbient = lerp(groundBounce, skyZenith, hemi) * 0.45f;

    // Specular IBL sheen with roughness decay and shadow specular occlusion (removes unwanted reflection shine in shadows)
    float specRoughDecay = pow(saturate(1.0f - roughness), 3.0f);
    float specOcclusion = lerp(0.15f, 1.0f, shadowFactor);
    float3 iblSpecular = skyReflection * F_env * specRoughDecay * specOcclusion;
    float ambientCloudMod = lerp(0.70f, 1.0f, cloudShadow);
    float3 diffuseAlbedo=albedo*(1-metallic)*(1-F0);
    float3 ambient = (DiffuseEnvironment(N,diffuseAlbedo,LightDir,LightColor)+iblSpecular)*ambientCloudMod*ao;

    float3 emission = albedo * matEmission;
    if (emissiveIndex > 0 && emissiveIndex < 1000) {
        float3 emitTex = g_Textures[emissiveIndex].Sample(g_Sampler, texUV).rgb;
        emission += emitTex * max(matEmission, 1.0f);
    }

    float3 finalColor = ambient + directLight + emission;

    if (!all(isfinite(finalColor))) {
        finalColor = float3(0.0f, 0.0f, 0.0f);
    }
    finalColor = min(finalColor, 16.0f);
    outVals.color = float4(finalColor, 1.0f);

    float2 curNDC = (abs(input.curPos.w) > 0.001f) ? (input.curPos.xy / input.curPos.w) : float2(0.0f, 0.0f);
    float2 prevNDC = (abs(input.prevPos.w) > 0.001f) ? (input.prevPos.xy / input.prevPos.w) : float2(0.0f, 0.0f);
    // Only compute motion if both positions are in front of near plane
    float2 curUV  = curNDC  * float2(0.5f, -0.5f) + 0.5f;
    float2 prevUV = prevNDC * float2(0.5f, -0.5f) + 0.5f;
    float2 motionVec = float2(0.0f, 0.0f);
    if (input.curPos.w > 0.001f && input.prevPos.w > 0.001f) {
        motionVec = prevUV - curUV;   // DLSS expects current->previous direction
    }
    // Sanitize: reject NaN, Inf, and unreasonably large vectors (> half screen)
    if (!all(isfinite(motionVec)) || any(abs(motionVec) > 0.5f)) {
        motionVec = float2(0.0f, 0.0f);
    }
    outVals.velocity = float4(motionVec, 0.0f, 0.0f);
    outVals.normal = float4(N, roughness);
    outVals.diffuseAlbedo=float4(diffuseAlbedo,ao);
    outVals.specularAlbedo=float4(F0,1);

    return outVals;
}
