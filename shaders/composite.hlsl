#include "environment_common.hlsl"
#include "cloud_common.hlsl"
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

struct SceneConstantBuffer {
    row_major float4x4 viewProj;
    row_major float4x4 prevViewProj;
    row_major float4x4 cleanViewProj;
    row_major float4x4 prevCleanViewProj;
    row_major float4x4 model;
    row_major float4x4 sunViewProj;
    float3   cameraPos;
    float    pad1;
    float3   lightDir;
    float    pad2;
    float3   lightColor;
    float    pad3;
    float2   screenRes;
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

ConstantBuffer<SceneConstantBuffer> g_Constants : register(b0);
Texture2D gSceneColor    : register(t0);
Texture2D gSSAO          : register(t1);
Texture2D gSSGI          : register(t2);
Texture2D gRayTracingOut : register(t3);
Texture2D<float4> gNormal : register(t4);
Texture2D<float> gDepth : register(t5);
Texture2D<float4> gDiffuse : register(t6);

struct VertexOut {
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VSMain(uint VertexId : SV_VertexID) {
    VertexOut vout;
    vout.TexC = float2((VertexId << 1) & 2, VertexId & 2);
    vout.PosH = float4(vout.TexC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PSMain(VertexOut pin) : SV_Target {
    int2 pixel = int2(pin.PosH.xy);
    float3 scene = gSceneColor.Load(int3(pixel, 0)).rgb;
    float ssao = gSSAO.Load(int3(pixel, 0)).r;
    float3 ssgi = gSSGI.Load(int3(pixel, 0)).rgb;
    float4 rt = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [branch]
    if (g_Constants.pad3 > 0.5f) {
        rt = gRayTracingOut.Load(int3(pixel, 0));
    }

    float depth=gDepth.Load(int3(pixel,0));
    float3 color=scene;
    float3 indirect=0;
    if(depth<.999999f){
        float4 normalRough=gNormal.Load(int3(pixel,0));
        float3 normal=normalRough.xyz;
        float4 diffuse=gDiffuse.Load(int3(pixel,0));
        if(dot(normal,normal)>.1f){
            normal=normalize(normal);
            float2 uv=(float2(pixel)+.5f)/g_Constants.screenRes;
            // Composite model matrix contains the inverse jittered view-projection.
            float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),depth,1),g_Constants.model);
            float3 position=world.xyz/world.w;
            float cloud=GetCloudShadow(position,g_Constants.lightDir,g_Constants.cloudTime,g_Constants.cloudDensity);
            float3 ambient=DiffuseEnvironment(normal,diffuse.rgb,g_Constants.lightDir,g_Constants.lightColor)*diffuse.a*lerp(.70f,1.f,cloud);
            float ao=saturate(ssao);
            // AO affects indirect diffuse; preserve direct lamps and emission.
            color-=min(scene,ambient)*(1-ao);
            float3 filtered=0;float weightSum=0;
            int radius=rt.a>.5f?4:1;
            int stride=1;
            [loop]for(int y=-radius;y<=radius;++y)[loop]for(int x=-radius;x<=radius;++x){
                int2 p=clamp(pixel+int2(x,y)*stride,int2(0,0),int2(g_Constants.screenRes)-1);
                float d=gDepth.Load(int3(p,0));float3 n=gNormal.Load(int3(p,0)).xyz;
                float3 albedoDifference=gDiffuse.Load(int3(p,0)).rgb-diffuse.rgb;
                float2 neighborUV=(float2(p)+.5f)/g_Constants.screenRes;
                float4 neighborWorld=mul(float4(neighborUV*float2(2,-2)+float2(-1,1),d,1),g_Constants.model);
                float planeError=abs(dot(neighborWorld.xyz/neighborWorld.w-position,normal));
                float weight=pow(saturate(dot(normal,n)),24)*exp(-planeError/.05f);
                weight*=exp(-dot(albedoDifference,albedoDifference)*20)*exp(-float(x*x+y*y)/12);
                filtered+=gSSGI.Load(int3(p,0)).rgb*weight;weightSum+=weight;
            }
            indirect=filtered/max(weightSum,.0001f);
            color+=indirect*ao;
        }
    }
    // If DXR raytracing hit geometry, apply full raytraced image (with DXR shadows & reflections)
    if (rt.a > 0.5f) {
        color = rt.rgb+indirect;
    }
    
    if (!all(isfinite(color))) {
        color = float3(0.0f, 0.0f, 0.0f);
    }
    color = clamp(color, 0.0f, 16.0f);
    return float4(color, 1.0f);
}
