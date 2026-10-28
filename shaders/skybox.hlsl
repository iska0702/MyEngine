cbuffer SkyboxConstantBuffer : register(b0)
{
    row_major float4x4 InvViewProj;
    float4 LightColor;
    float4 LightDir;
    float4 SkyColor;
};

#define PI 3.14159265359

struct VSInput
{
    uint VertexID : SV_VertexID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput result;
    float2 texcoord = float2((input.VertexID << 1) & 2, input.VertexID & 2);
    result.position = float4(texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    result.position.z = 1.0f;
    float4 unprojected = mul(result.position, InvViewProj);
    result.viewDir = unprojected.xyz / unprojected.w;
    return result;
}

struct PSOutput
{
    float4 color : SV_TARGET0;
    float4 velocity : SV_TARGET1;
};

PSOutput PSMain(PSInput input)
{
    float3 dir = normalize(input.viewDir);
    float t = saturate(dir.y);
    float tPow = pow(t, 0.6f);

    // Определяем день/ночь по положению солнца и яркости
    float sunElev = -LightDir.y;
    float intensity = dot(LightColor.rgb, float3(0.333f, 0.333f, 0.333f));
    if (intensity <= 0.0001f) {
        PSOutput output;
        output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.velocity = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return output;
    }

    float cloudDensity = LightColor.w;
    float overcast = saturate((cloudDensity - 0.85f) / 1.60f);

    float nightWeight = saturate((0.15f - intensity) * 10.0f);
    if (sunElev < 0.0f) {
        nightWeight = max(nightWeight, saturate(-sunElev * 5.0f));
    }
    float nonNightWeight = (1.0f - nightWeight) * saturate(sunElev * 5.0f + 0.1f);

    // Базовый цвет неба (управляется через SkyColor, LightColor и Overcast)
    float3 nightHorizon = float3(0.012f, 0.024f, 0.055f) * min(intensity * 3.0f, 1.0f);
    float3 nightZenith = float3(0.004f, 0.008f, 0.020f) * min(intensity * 3.0f, 1.0f);
    
    float3 skyBase = lerp(SkyColor.rgb, nightHorizon, nightWeight);
    float3 horizonColor = lerp(skyBase * 0.7f, nightHorizon, nightWeight);
    float3 zenithColor = lerp(skyBase * 1.3f, nightZenith, nightWeight);

    // Rainy Overcast Sky Palette (Atmospheric Slate Gray & Wet Mist Absorption)
    float3 stormHorizon = float3(0.24f, 0.28f, 0.38f) * min(intensity * 2.0f, 1.0f);
    float3 stormZenith = float3(0.12f, 0.15f, 0.22f) * min(intensity * 2.0f, 1.0f);
    horizonColor = lerp(horizonColor, stormHorizon, overcast * 0.85f);
    zenithColor = lerp(zenithColor, stormZenith, overcast * 0.85f);

    float3 skyGradient = lerp(horizonColor, zenithColor, tPow);

    // Источник света (Солнце днём / Луна ночью)
    float3 lightDir = normalize(-LightDir.xyz);
    // Twilight is directional: warm solar horizon, cool opposite hemisphere.
    float dusk=smoothstep(-.18f,.01f,sunElev)*(1-smoothstep(.12f,.40f,sunElev));
    float2 sunHorizontal=lightDir.xz/max(length(lightDir.xz),.001f);
    float2 viewHorizontal=dir.xz/max(length(dir.xz),.001f);
    float towardSun=dot(viewHorizontal,sunHorizontal);
    float warmLobe=pow(saturate((towardSun+.15f)/1.15f),2.5f);
    float horizonGlow=exp(-max(dir.y,0)*4.0f);
    float3 coolTwilight=lerp(float3(.075f,.095f,.18f),float3(.035f,.065f,.15f),tPow);
    float3 duskSky=coolTwilight*lerp(.55f,1.0f,saturate(towardSun*.5f+.5f));
    duskSky+=float3(.65f,.22f,.065f)*warmLobe*horizonGlow*(1-overcast*.65f);
    skyGradient=lerp(skyGradient,duskSky,dusk);

    float bodyDot = dot(dir, lightDir);
    float bodyDist = acos(clamp(bodyDot, -1.0f, 1.0f));

    float3 celestialGlow = float3(0.0f, 0.0f, 0.0f);
    if (nonNightWeight > 0.05f)
    {
        // Дневное солнце (рассеивается и смягчается под дождевыми облаками)
        float scatterExponent = lerp(3.5f, 1.6f, overcast);
        float scatter = exp(-bodyDist * scatterExponent);
        celestialGlow += LightColor.rgb * scatter * lerp(1.0f, 0.35f, overcast) * nonNightWeight;
        
        float diskThreshold = lerp(0.9985f, 0.9930f, overcast);
        float disk = smoothstep(diskThreshold, 0.9996f, bodyDot);
        celestialGlow += LightColor.rgb * lerp(15.0f, 1.8f, overcast) * disk * nonNightWeight;
    }
    
    if (nightWeight > 0.05f && intensity > 0.005f)
    {
        // Ночная Луна и лунный ореол
        float moonScatter = exp(-bodyDist * 3.5f);
        celestialGlow += LightColor.rgb * float3(0.5f, 0.7f, 1.0f) * moonScatter * 0.6f * nightWeight;
        float moonHalo = exp(-bodyDist * 12.0f);
        celestialGlow += LightColor.rgb * float3(0.6f, 0.8f, 1.0f) * moonHalo * 0.8f * nightWeight;
        float moonDisk = smoothstep(0.9982f, 0.9992f, bodyDot);
        celestialGlow += LightColor.rgb * 4.0f * moonDisk * nightWeight;
    }

    // Звёзды (только ночью, выше горизонта)
    float3 starColor = float3(0.0f, 0.0f, 0.0f);
    if (nightWeight > 0.01f && dir.y > 0.02f)
    {
        float yaw = atan2(dir.x, dir.z);
        float pitch = asin(clamp(dir.y, -1.0f, 1.0f));
        float cellsX = 400.0f, cellsY = 150.0f;
        float2 gridPos = float2(yaw / (2.0f * PI) * cellsX, (pitch / PI + 0.5f) * cellsY);
        float2 cellId = floor(gridPos);
        float2 cellUv = frac(gridPos);
        float starHash = frac(sin(dot(cellId, float2(12.9898f, 78.233f))) * 43758.5453f);
        if (starHash > 0.995f)
        {
            float2 starPos = float2(frac(starHash * 34.56f), frac(starHash * 78.91f));
            starPos = clamp(starPos, 0.2f, 0.8f);
            float dist = length(cellUv - starPos);
            float starIntensity = smoothstep(0.04f, 0.0f, dist) * lerp(0.3f, 1.0f, frac(starHash * 987.65f));
            float colorHash = frac(starHash * 123.45f);
            float3 sCol = lerp(float3(0.6f, 0.8f, 1.0f), float3(1.0f, 0.95f, 0.85f), colorHash);
            starColor += starIntensity * sCol * nightWeight;
        }
    }

    // Смешиваем всё
    float3 finalSky = skyGradient + celestialGlow + starColor;

    // Защита от NaN
    if (!any(isfinite(finalSky))) finalSky = float3(0.05f, 0.1f, 0.2f);
    finalSky = max(0.0f, finalSky);

    // Лёгкая экспозиция
    float3 hdrColor = finalSky * 1.5f;

    PSOutput output;
    output.color = float4(hdrColor, 1.0f);
    output.velocity = float4(0.0f, 0.0f, 0.0f, 0.0f);
    return output;
}