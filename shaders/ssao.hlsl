// Screen-Space Ambient Occlusion Shader (Clean, artifact-free, thickness-aware)

cbuffer SSAOConstants : register(b0)
{
    matrix Projection;
    matrix InvProjection;
    matrix View;
    float4 Samples[32]; // Hemisphere kernel samples
    float4 NoiseScale;  // xy = screenSize / noiseTexSize, zw = 1.0/screenSize
    float2 uvScale;     // Scale for reading depth in DLSS mode
    float  Radius;
    float  Bias;
    float  Intensity;
    float  Padding;
};

Texture2D<float> gDepth : register(t0);
SamplerState gSamplerPoint : register(s0);

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

// Reconstruct view-space position from depth
float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    ndc.y = -ndc.y; // D3D Y-flip
    float4 viewPos = mul(ndc, InvProjection);
    return viewPos.xyz / max(viewPos.w, 0.00001f);
}

// Interleaved Gradient Noise for film-quality blue-noise dithering
float InterleavedGradientNoise(float2 screenPos) {
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// Robust, artifact-free normal reconstruction from depth buffer
float3 ReconstructNormal(float2 uv, float centerDepth, float3 centerPos)
{
    float2 offset = NoiseScale.zw; // 1.0 / screenSize
    
    float dL = gDepth.SampleLevel(gSamplerPoint, uv - float2(offset.x, 0.0f), 0);
    float dR = gDepth.SampleLevel(gSamplerPoint, uv + float2(offset.x, 0.0f), 0);
    float dU = gDepth.SampleLevel(gSamplerPoint, uv - float2(0.0f, offset.y), 0);
    float dD = gDepth.SampleLevel(gSamplerPoint, uv + float2(0.0f, offset.y), 0);
    
    float3 pL = ReconstructViewPos(uv - float2(offset.x, 0.0f), dL);
    float3 pR = ReconstructViewPos(uv + float2(offset.x, 0.0f), dR);
    float3 pU = ReconstructViewPos(uv - float2(0.0f, offset.y), dU);
    float3 pD = ReconstructViewPos(uv + float2(0.0f, offset.y), dD);
    
    // Choose smallest horizontal and vertical vectors to avoid depth discontinuities
    float3 dx1 = pR - centerPos;
    float3 dx2 = centerPos - pL;
    float3 dx = (dot(dx1, dx1) < dot(dx2, dx2)) ? dx1 : dx2;
    
    float3 dy1 = pD - centerPos;
    float3 dy2 = centerPos - pU;
    float3 dy = (dot(dy1, dy1) < dot(dy2, dy2)) ? dy1 : dy2;
    
    float3 n = cross(dx, dy);
    float lenSq = dot(n, n);
    if (lenSq < 0.000001f) {
        return float3(0.0f, 0.0f, -1.0f);
    }
    
    float3 normal = normalize(n);
    if (dot(normal, centerPos) > 0.0f) normal = -normal;
    return normal;
}

float4 PSMain(VertexOut pin) : SV_Target
{
    float2 scaledUV = pin.TexC * uvScale;
    float depth = gDepth.Sample(gSamplerPoint, scaledUV);
    
    // Skip sky pixels
    if (depth >= 0.9999f) return float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    float3 viewPos = ReconstructViewPos(scaledUV, depth);
    float3 normal = ReconstructNormal(scaledUV, depth, viewPos);
    
    // Blue-noise rotation angle around normal
    float ign = InterleavedGradientNoise(pin.PosH.xy);
    float angle = ign * 6.2831853f;
    float cosA = cos(angle);
    float sinA = sin(angle);
    
    // Construct orthonormal basis around surface normal
    float3 up = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    
    // Rotate tangent space by random angle
    float3 rotTangent = tangent * cosA + bitangent * sinA;
    float3 rotBitangent = cross(normal, rotTangent);
    float3x3 TBN = float3x3(rotTangent, rotBitangent, normal);
    
    float effectiveRadius = max(Radius, 0.20f);
    float effectiveBias = max(Bias, 0.015f);
    float3 biasedViewPos = viewPos + normal * effectiveBias;
    
    float occlusion = 0.0f;
    float totalWeight = 0.0f;
    
    [unroll(32)]
    for (int i = 0; i < 32; i++)
    {
        float3 sampleDir = mul(Samples[i].xyz, TBN);
        float3 samplePos = biasedViewPos + sampleDir * effectiveRadius;
        
        // Project sample point to screen
        float4 clipPos = mul(float4(samplePos, 1.0f), Projection);
        if (clipPos.w <= 0.0001f) continue;
        
        float2 sampleUV = (clipPos.xy / clipPos.w) * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) continue;
        
        float sampleDepth = gDepth.SampleLevel(gSamplerPoint, sampleUV * uvScale, 0);
        if (sampleDepth >= 0.9999f) continue;
        
        float3 hitViewPos = ReconstructViewPos(sampleUV * uvScale, sampleDepth);
        
        // Distance from current surface point to occluding geometry
        float3 occluderVec = hitViewPos - viewPos;
        float distToOccluder = length(occluderVec);
        
        // Geometry must be in front of sample point (closer to camera than samplePos)
        float depthDiff = samplePos.z - hitViewPos.z;
        
        // Thickness check: ignore geometry that is too far behind the sample to prevent halos/edge-blackening
        float maxThickness = effectiveRadius * 1.5f;
        if (depthDiff > effectiveBias && depthDiff < maxThickness)
        {
            // Range attenuation: smoothly falls off as occluder distance reaches radius
            float rangeFactor = saturate(1.0f - distToOccluder / effectiveRadius);
            float angleFactor = saturate(dot(normal, occluderVec / max(distToOccluder, 0.0001f)));
            
            occlusion += rangeFactor * angleFactor;
        }
        totalWeight += 1.0f;
    }
    
    float ao = 1.0f;
    if (totalWeight > 0.001f) {
        float rawAO = (occlusion / totalWeight) * Intensity;
        ao = saturate(1.0f - rawAO);
    }
    
    // Prevent unphysical pure-black voids (soft ambient minimum floor)
    ao = clamp(ao, 0.20f, 1.0f);
    
    return float4(ao, ao, ao, 1.0f);
}
