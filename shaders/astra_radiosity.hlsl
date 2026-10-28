// Astra Radiosity: Surface Cache Indirect Lighting Update
// Uses DXR Inline Ray Query (SM 6.5)

struct SceneConstantBuffer
{
    row_major float4x4 viewProj;
    row_major float4x4 prevViewProj;
    row_major float4x4 cleanViewProj;
    row_major float4x4 prevCleanViewProj;
    row_major float4x4 model;
    row_major float4x4 sunViewProj;
    float3 cameraPos;
    float pad1;
    float3 lightDir;
    float pad2;
    float3 lightColor;
    float pad3;
};

ConstantBuffer<SceneConstantBuffer> g_Constants : register(b0);
RaytracingAccelerationStructure g_Scene : register(t0);
Texture2D<float4> g_AlbedoAtlas : register(t1);
Texture2D<float4> g_NormalAtlas : register(t2);
RWTexture2D<float4> g_IrradianceAtlas : register(u0);

SamplerState g_LinearSampler : register(s0);

// Simple pseudo-random generator
float Hash(uint n) {
    n = (n << 13) ^ n;
    return float(1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
}

float3 GetOrthoVector(float3 n) {
    float3 v = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    return normalize(cross(v, n));
}

float3 SampleHemisphere(float3 n, float2 rand) {
    float phi = 2.0 * 3.1415926 * rand.x;
    float cosTheta = sqrt(rand.y);
    float sinTheta = sqrt(1.0 - rand.y);
    
    float3 t = GetOrthoVector(n);
    float3 b = cross(n, t);
    
    return normalize(t * cos(phi) * sinTheta + b * sin(phi) * sinTheta + n * cosTheta);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint2 texCoord = dtid.xy;
    if (texCoord.x >= 8192 || texCoord.y >= 8192) return;

    float4 albedo = g_AlbedoAtlas[texCoord];
    if (albedo.a == 0.0) return; // Skip non-resident/empty texels

    float3 normal = g_NormalAtlas[texCoord].xyz * 2.0 - 1.0;
    if (!all(isfinite(normal)) || dot(normal, normal) < 0.000001f) {
        g_IrradianceAtlas[texCoord] = float4(0, 0, 0, 0);
        return;
    }
    normal = normalize(normal);

    // Total atlas size is 8192
    float2 rvtUV = float2(texCoord) / 8192.0f;
    float3 worldPos;
    worldPos.x = rvtUV.x * 200.0f - 100.0f;
    worldPos.z = rvtUV.y * 200.0f - 100.0f;
    worldPos.y = 0; // Derive from G-Buffer or Depth Atlas if available
    
    float3 accumulatedIrradiance = 0;
    const uint numRays = 4; // Low count for prototype sustainability
    
    for (uint i = 0; i < numRays; ++i) {
        float2 rand = float2(Hash(dtid.x + dtid.y * 8192 + i), Hash(dtid.y + dtid.x * 8192 + i + 123));
        float3 rayDir = SampleHemisphere(normal, rand);
        
        RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
        RayDesc ray;
        ray.Origin = worldPos + normal * 0.1f; // Enhanced bias to avoid self-shadowing
        ray.Direction = rayDir;
        ray.TMin = 0.001;
        ray.TMax = 1000.0;
        
        q.TraceRayInline(g_Scene, RAY_FLAG_NONE, 0xFF, ray);
        q.Proceed();
        
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            // Hit! Sample Astra Surface Cache at hit point (if available)
            // For this prototype, we use a bounce approximation
            accumulatedIrradiance += g_Constants.lightColor * 0.2f * albedo.rgb;
        } else {
            // Sky contribution
            accumulatedIrradiance += g_Constants.lightColor * 0.5f * max(rayDir.y, 0.0);
        }
    }
    
    // Energy Conservation & Normalization
    float3 newIrr = (accumulatedIrradiance / float(numRays)) * 0.95f; // Conservation factor
    float3 oldIrr = g_IrradianceAtlas[texCoord].rgb;
    if (!all(isfinite(oldIrr))) oldIrr = float3(0, 0, 0);
    
    // Temporal accumulation for stability
    g_IrradianceAtlas[texCoord] = float4(lerp(oldIrr, newIrr, 0.1f), 1.0f);
}
