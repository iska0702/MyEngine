// Astra TAA (Temporal Anti-Aliasing) Compute Shader
// Performs neighborhood clamping and velocity-based reprojection

Texture2D<float4> g_CurrentColor : register(t0);
Texture2D<float2> g_Velocity     : register(t1);
Texture2D<float4> g_HistoryRead  : register(t2);   // Read-only history for bilinear reprojection
RWTexture2D<float4> g_History    : register(u0);   // Write-only history output

SamplerState g_LinearSampler : register(s0);

cbuffer SceneConstants : register(b0)
{
    matrix ViewProj;
    matrix InvViewProj;
    float3 CameraPos;
    float JitterX;
    float JitterY;
    float DeltaTime;
};

// Convert RGB to YCoCg for better clamping
float3 RGBToYCoCg(float3 rgb)
{
    float y  =  0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b;
    float co =  0.5  * rgb.r - 0.5 * rgb.b;
    float cg = -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b;
    return float3(y, co, cg);
}

float3 YCoCgToRGB(float3 ycocg)
{
    float r = ycocg.x + ycocg.y - ycocg.z;
    float g = ycocg.x + ycocg.z;
    float b = ycocg.x - ycocg.y - ycocg.z;
    return float3(r, g, b);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    g_CurrentColor.GetDimensions(width, height);

    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    float2 texelSize = 1.0f / float2(width, height);
    float2 uv = (float2(dispatchThreadID.xy) + 0.5f) * texelSize;

    // 1. Sample Current Neighborhood (3x3)
    float3 m1 = 0;
    float3 m2 = 0;
    
    // Find min/max in neighborhood to clamp history
    float3 minColor = 9999.0f;
    float3 maxColor = -9999.0f;

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            int2 pixel = clamp(int2(dispatchThreadID.xy) + int2(x, y), int2(0,0), int2(width,height)-1);
            float3 c = g_CurrentColor.Load(int3(pixel, 0)).rgb;
            float3 ycocg = RGBToYCoCg(c);
            minColor = min(minColor, ycocg);
            maxColor = max(maxColor, ycocg);
            m1 += ycocg;
            m2 += ycocg * ycocg;
        }
    }

    // 2. Fetch Motion Vector and Reproject
    float2 velocity = g_Velocity.Load(int3(dispatchThreadID.xy, 0)).xy;
    float2 historyUV = uv + velocity;

    float4 currentSample = g_CurrentColor[dispatchThreadID.xy];
    float3 currentYCoCg = RGBToYCoCg(currentSample.rgb);

    // 3. Sample History at reprojected UV with bilinear filtering
    if (historyUV.x < 0.001f || historyUV.x > 0.999f || historyUV.y < 0.001f || historyUV.y > 0.999f)
    {
        // Out of bounds - use current frame only, no blending
        g_History[dispatchThreadID.xy] = currentSample;
        return;
    }

    // Bilinear sample at reprojected position (the actual fix for ghosting/white lines)
    float3 historySample = g_HistoryRead.SampleLevel(g_LinearSampler, historyUV, 0).rgb;
    float3 historyYCoCg = RGBToYCoCg(historySample);

    // Variance-based clipping/clamping
    float3 mu = m1 / 9.0;
    float3 sigma = sqrt(abs(m2 / 9.0 - mu * mu));
    float gamma = 1.0; // Tuning parameter for clamping tightness
    minColor = mu - gamma * sigma;
    maxColor = mu + gamma * sigma;

    historyYCoCg = clamp(historyYCoCg, minColor, maxColor);

    // 4. Blend
    // Reduce feedback when moving fast to avoid ghosting/white-line artifacts
    float velLen = length(velocity * float2(width, height));
    // feedback: 0.92 at rest, drops to 0.7 at high speed
    float feedback = lerp(0.92f, 0.70f, saturate(velLen / 8.0f));

    float3 resolvedYCoCg = lerp(currentYCoCg, historyYCoCg, feedback);
    float3 resolvedRGB = YCoCgToRGB(resolvedYCoCg);
    // Prevent NaN/Inf from leaking into history
    if (!all(isfinite(resolvedRGB))) resolvedRGB = currentSample.rgb;
    resolvedRGB = clamp(resolvedRGB, 0.0f, 32.0f);

    g_History[dispatchThreadID.xy] = float4(resolvedRGB, currentSample.a);
}
