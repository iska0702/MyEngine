// RVT Bake Shader
struct VSInput {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    float3 normal   : NORMAL;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 normal   : NORMAL;
};

cbuffer BakeConstants : register(b0)
{
    float4x4 PageTransform; // Transforms world-space to page-space UV
    uint PageIndex;
};

Texture2D g_Textures[16] : register(t0);
SamplerState g_Sampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput output;
    // Map input position directly to page-space UV
    output.position = mul(float4(input.position, 1.0f), PageTransform);
    output.uv = input.uv;
    output.normal = input.normal;
    return output;
}

struct PSOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    
    // Sample multiple layers with their height maps
    float4 layer0Albedo = g_Textures[0].Sample(g_Sampler, input.uv);
    float  layer0Height = g_Textures[0].Sample(g_Sampler, input.uv).a; // Height stored in Alpha
    
    float4 layer1Albedo = g_Textures[1].Sample(g_Sampler, input.uv);
    float  layer1Height = g_Textures[1].Sample(g_Sampler, input.uv).a;
    
    // Height Blend logic
    float blendFactor = saturate(input.uv.x); // Base linear gradient
    float contrast = 0.1f;
    
    float v1 = layer0Height + (1.0 - blendFactor);
    float v2 = layer1Height + blendFactor;
    
    float depth = 0.1;
    float finalBlend = saturate((v2 - v1 + depth) / (2.0 * depth));
    
    output.albedo = lerp(layer0Albedo, layer1Albedo, finalBlend);
    output.normal = float4(input.normal * 0.5f + 0.5f, 1.0f);
    
    return output;
}
