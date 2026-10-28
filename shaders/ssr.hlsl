// Astra HiZ-Accelerated Screen Space Reflections (SSR)
// Performs efficient raymarching using the Hierarchical Depth Buffer

struct SSRConstants {
    float4x4 Projection;
    float4x4 InvProjection;
    float4x4 View;
    float4x4 InvView;
    float4x4 NormalMatrix;
    float2 ScreenRes;
    float RayStepSize;
    float MaxRayDistance;
    float Thickness; // Estimated depth thickness for intersection
    float RoughnessThreshold;
    float2 padding;
};

ConstantBuffer<SSRConstants> g_SSRConsts : register(b0);
Texture2D<float> g_HiZ : register(t0); // Hierarchical Depth Buffer
RWTexture2D<float4> g_ReflectionOutput : register(u0);
SamplerState g_PointClamp : register(s0);

// Helper to convert clip space to view space
float3 GetViewPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y *= -1.0;
    float4 viewPos = mul(g_SSRConsts.InvProjection, clipPos);
    return viewPos.xyz / viewPos.w;
}

// Hierarchical Z-Buffer Raymarching (Basic version)
bool RaymarchHiZ(float3 rayOrigin, float3 rayDir, out float2 hitUV, out float hitZ) {
    hitUV = 0.0;
    hitZ = 0.0;
    float3 currentPos = rayOrigin;
    float3 stepDir = rayDir * g_SSRConsts.RayStepSize;
    
    for (int i = 0; i < 64; ++i) { // Limit steps
        currentPos += stepDir;
        
        // Convert currentPos to UV
        float4 clipPos = mul(g_SSRConsts.Projection, float4(currentPos, 1.0));
        float3 ndc = clipPos.xyz / clipPos.w;
        float2 uv = ndc.xy * 0.5 + 0.5;
        uv.y = 1.0 - uv.y;
        
        if (any(uv < 0) || any(uv > 1)) return false;
        
        float sampledDepth = g_HiZ.SampleLevel(g_PointClamp, uv, 0);
        if (ndc.z > sampledDepth && (ndc.z - sampledDepth) < g_SSRConsts.Thickness) {
            hitUV = uv;
            hitZ = ndc.z;
            return true;
        }
    }
    return false;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= g_SSRConsts.ScreenRes)) return;

    float2 uv = (float2(id.xy) + 0.5) / g_SSRConsts.ScreenRes;
    float depth = g_HiZ.Load(uint3(id.xy, 0));
    
    if (depth >= 1.0) {
        g_ReflectionOutput[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 viewPos = GetViewPos(uv, depth);
    float3 viewNormal = float3(0, 1, 0); // Placeholder, normally sample G-Buffer Normal
    
    float3 reflectDir = reflect(normalize(viewPos), normalize(viewNormal));
    
    if (reflectDir.z < 0) { // Ray points away from camera
        g_ReflectionOutput[id.xy] = float4(0, 0, 0, 0);
        return;
    }

    float2 hitUV;
    float hitZ;
    if (RaymarchHiZ(viewPos, reflectDir, hitUV, hitZ)) {
        // Sample color from scene color at hitUV (normally use a dedicated radiance texture)
        g_ReflectionOutput[id.xy] = float4(hitUV, hitZ, 1.0); // Pack hit data for resolve
    } else {
        g_ReflectionOutput[id.xy] = float4(0, 0, 0, 0); // Fallback to DXR
    }
}
