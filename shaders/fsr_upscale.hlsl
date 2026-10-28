// FidelityFX Super Resolution (FSR) - EASU + RCAS Fullscreen Upscaling
Texture2D<float4> g_InputColor : register(t0);
SamplerState g_LinearSampler   : register(s0);

cbuffer FSRConstants : register(b0)
{
    float2 InputSize;       // Render size (m_renderWidth, m_renderHeight)
    float2 OutputSize;      // Display size (m_width, m_height)
    float2 InputTexelSize;  // (1/m_renderWidth, 1/m_renderHeight)
    float2 OutputTexelSize; // (1/m_width, 1/m_height)
    float  Sharpness;       // RCAS sharpness (0.0 to 1.0)
    float  Padding0;
    float2 Padding1;
};

struct VertexOut
{
    float4 PosH    : SV_POSITION;
    float2 TexC    : TEXCOORD;
};

VertexOut VSMain(uint vid : SV_VertexID)
{
    VertexOut vout;
    float2 grid = float2((vid == 2) ? 3.0f : -1.0f, (vid == 1) ? 3.0f : -1.0f);
    vout.PosH = float4(grid, 0.0f, 1.0f);
    vout.TexC = float2((grid.x + 1.0f) * 0.5f, 1.0f - (grid.y + 1.0f) * 0.5f);
    return vout;
}

// 12-Tap Edge Adaptive Directional Kernel (EASU)
float3 FSR_EASU(float2 uv)
{
    float2 pp = uv * InputSize - 0.5f;
    float2 fp = floor(pp);
    float2 f = pp - fp;
    
    float2 p0 = (fp + 0.5f) * InputTexelSize;
    
    float3 c00 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2(-1.0f, -1.0f) * InputTexelSize, 0).rgb;
    float3 c10 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2( 0.0f, -1.0f) * InputTexelSize, 0).rgb;
    float3 c20 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2( 1.0f, -1.0f) * InputTexelSize, 0).rgb;
    
    float3 c01 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2(-1.0f,  0.0f) * InputTexelSize, 0).rgb;
    float3 c11 = g_InputColor.SampleLevel(g_LinearSampler, p0, 0).rgb;
    float3 c21 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2( 1.0f,  0.0f) * InputTexelSize, 0).rgb;
    
    float3 c02 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2(-1.0f,  1.0f) * InputTexelSize, 0).rgb;
    float3 c12 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2( 0.0f,  1.0f) * InputTexelSize, 0).rgb;
    float3 c22 = g_InputColor.SampleLevel(g_LinearSampler, p0 + float2( 1.0f,  1.0f) * InputTexelSize, 0).rgb;
    
    float3 lumaWeight = float3(0.299f, 0.587f, 0.114f);
    float l00 = dot(c00, lumaWeight);
    float l10 = dot(c10, lumaWeight);
    float l20 = dot(c20, lumaWeight);
    float l01 = dot(c01, lumaWeight);
    float l11 = dot(c11, lumaWeight);
    float l21 = dot(c21, lumaWeight);
    float l02 = dot(c02, lumaWeight);
    float l12 = dot(c12, lumaWeight);
    float l22 = dot(c22, lumaWeight);
    
    float dirX = (l20 + 2.0f * l21 + l22) - (l00 + 2.0f * l01 + l02);
    float dirY = (l02 + 2.0f * l12 + l22) - (l00 + 2.0f * l10 + l20);
    float edgeLen = sqrt(dirX * dirX + dirY * dirY);
    
    float3 bilinearColor = lerp(lerp(c11, c21, f.x), lerp(c12, c22, f.x), f.y);
    
    float2 dir = (edgeLen > 0.001f) ? float2(-dirY, dirX) / edgeLen : float2(0, 0);
    float3 edgeSample1 = g_InputColor.SampleLevel(g_LinearSampler, uv + dir * InputTexelSize * 0.75f, 0).rgb;
    float3 edgeSample2 = g_InputColor.SampleLevel(g_LinearSampler, uv - dir * InputTexelSize * 0.75f, 0).rgb;
    float3 edgeFiltered = (edgeSample1 + edgeSample2 + c11 * 2.0f) * 0.25f;
    
    float edgeWeight = clamp(edgeLen * 3.5f, 0.0f, 0.85f);
    return lerp(bilinearColor, edgeFiltered, edgeWeight);
}

// RCAS: Robust Contrast-Adaptive Sharpening
float3 FSR_RCAS(float3 color, float2 uv)
{
    float3 e = color;
    float3 b = g_InputColor.SampleLevel(g_LinearSampler, uv + float2(0.0f, -InputTexelSize.y), 0).rgb;
    float3 d = g_InputColor.SampleLevel(g_LinearSampler, uv + float2(-InputTexelSize.x, 0.0f), 0).rgb;
    float3 f = g_InputColor.SampleLevel(g_LinearSampler, uv + float2(InputTexelSize.x, 0.0f), 0).rgb;
    float3 h = g_InputColor.SampleLevel(g_LinearSampler, uv + float2(0.0f, InputTexelSize.y), 0).rgb;
    
    float3 lumaWeight = float3(0.299f, 0.587f, 0.114f);
    float bL = dot(b, lumaWeight);
    float dL = dot(d, lumaWeight);
    float eL = dot(e, lumaWeight);
    float fL = dot(f, lumaWeight);
    float hL = dot(h, lumaWeight);
    
    float minL = min(min(bL, dL), min(min(fL, hL), eL));
    float maxL = max(max(bL, dL), max(max(fL, hL), eL));
    
    float peak = -1.0f / (lerp(8.0f, 3.0f, clamp(Sharpness, 0.0f, 1.0f)));
    float lobe = max(-minL, maxL - 1.0f) / max(maxL - minL, 0.001f);
    float w = clamp(lobe * peak, -0.2f, 0.0f);
    
    float3 sharpened = (b + d + f + h) * w + e;
    sharpened /= (1.0f + 4.0f * w);
    return clamp(sharpened, 0.0f, 65000.0f);
}

float4 PSMain(VertexOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float3 upscaled = FSR_EASU(uv);
    float3 finalColor = FSR_RCAS(upscaled, uv);
    return float4(finalColor, 1.0f);
}
