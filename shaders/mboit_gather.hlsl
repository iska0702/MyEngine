struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

struct PSOutput {
    float4 moments : SV_Target0;
};

cbuffer CloudConstants : register(b0) {
    float4x4 invViewProj;
    float3 cameraPos;
    float time;
    float3 lightDir;
    float cloudDensity;
    float padding[40];
};

Texture2D depthTexture : register(t0);
SamplerState pointSampler : register(s0);

PSInput VSMain(uint vertexId : SV_VertexID) {
    PSInput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

PSOutput PSMain(PSInput input) {
    PSOutput output;
    // Gather pass is now a no-op; all work done in resolve
    output.moments = float4(0, 0, 0, 0);
    return output;
}
