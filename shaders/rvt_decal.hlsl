// RVT Decal Projection Shader
struct VSInput {
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
};

cbuffer DecalConstants : register(b0)
{
    float4x4 WorldToDecal; // Matrix to transform world space to decal local space
    float4x4 PageTransform; // Transforms world-space to page-space SV_POSITION
    uint TextureIndex;
    float4 DecalColor;
};

Texture2D g_DecalTextures[100] : register(t0);
SamplerState g_Sampler : register(s0);

PSInput VSMain(VSInput input)
{
    PSInput output;
    // PageTransform maps the unit cube to SV_POSITION in the RVT atlas
    output.position = mul(float4(input.position, 1.0f), PageTransform);
    output.uv = input.uv;
    
    // We pass the local position of the unit cube to the pixel shader
    // so we can use it for projection clipping
    output.worldPos = input.position; 
    return output;
}

struct PSOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput output;
    
    // In our simplified setup, input.worldPos is already in the [-1, 1] range 
    // for the decal volume because we rendered a unit cube.
    // We clip pixels outside the unit cube
    if (any(abs(input.worldPos) > 1.0)) discard;

    // UVs are derived from the local X and Y
    float2 localUV = input.worldPos.xy * 0.5 + 0.5;
    float4 texColor = g_DecalTextures[TextureIndex].Sample(g_Sampler, localUV);
    
    // Clip based on texture alpha
    if (texColor.a < 0.1) discard;

    float3 decalNormal = float3(0, 0, 1); // Projecting along Z
    
    output.albedo = texColor * DecalColor;
    output.normal = float4(decalNormal * 0.5f + 0.5f, texColor.a);
    
    return output;
}
