// SSAO Bilateral Blur Shader
// Edge-preserving blur that smooths SSAO noise while respecting depth discontinuities

Texture2D<float4> gSSAO  : register(t0);
Texture2D<float>  gDepth : register(t1);
SamplerState gSampler : register(s0);

cbuffer BlurConstants : register(b0)
{
    float2 TexelSize; // 1.0 / screenResolution
    float2 Padding;
};

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

float4 PSMain(VertexOut pin) : SV_Target
{
    float centerDepth = gDepth.Sample(gSampler, pin.TexC);
    
    // Skip sky pixels
    if (centerDepth >= 0.9999f)
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    float centerAO = gSSAO.Sample(gSampler, pin.TexC).r;
    float result = 0.0f;
    float totalWeight = 0.0f;
    
    // 5x5 bilateral blur kernel with adaptive depth-guided weighting
    [unroll]
    for (int x = -2; x <= 2; x++)
    {
        [unroll]
        for (int y = -2; y <= 2; y++)
        {
            float2 offset = float2(x, y) * TexelSize * 1.5f;
            float2 sampleUV = pin.TexC + offset;
            
            float sampleAO = gSSAO.Sample(gSampler, sampleUV).r;
            float sampleDepth = gDepth.Sample(gSampler, sampleUV);
            
            // Adaptive depth weighting: robust across corners and planes while preserving true silhouettes
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * 80.0f);
            
            // Spatial Gaussian weight
            float dist2 = float(x * x + y * y);
            float spatialWeight = exp(-dist2 * 0.20f);
            
            float weight = depthWeight * spatialWeight;
            result += sampleAO * weight;
            totalWeight += weight;
        }
    }
    
    result /= max(totalWeight, 0.0001f);
    
    return float4(result, result, result, 1.0f);
}
