struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer CloudConstants : register(b0) {
    float4x4 invViewProj;
    float3 cameraPos;
    float time;
    float3 lightDir;
    float cloudDensity;
};

Texture2D momentsTexture : register(t0);
SamplerState pointSampler : register(s0);

PSInput VSMain(uint vertexId : SV_VertexID) {
    PSInput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

Texture2D<float> depthTexture : register(t1);

#include "cloud_common.hlsl"

float4 PSMain(PSInput input) : SV_Target {
    if (cloudDensity <= 0.001f) return 0;
    // Use sample with UVs to be resolution-independent
    float rawDepth = depthTexture.SampleLevel(pointSampler, input.uv, 0).r;

    float2 ndc = input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 clipFar = float4(ndc, 1.0, 1.0);
    float4 worldFar = mul(clipFar, invViewProj);
    worldFar /= worldFar.w;
    
    float3 rayDir = normalize(worldFar.xyz - cameraPos);
    
    if (rayDir.y < 0.01) return float4(0, 0, 0, 0); // Avoid math edge cases on the horizon
    
    float2 layer = CloudLayerBounds(cloudDensity);
    float t1 = (layer.x - cameraPos.y) / rayDir.y;
    float t2 = (layer.y - cameraPos.y) / rayDir.y;
    float tEntry = min(t1, t2);
    float tExit = max(t1, t2);
    
    if (tExit < 0.0 || tEntry >= tExit) return float4(0, 0, 0, 0);
    tEntry = max(tEntry, 0.1);
    tExit = min(tExit, 40000.0f); 
    
    // Geometry depth testing
    float4 clipScene = float4(ndc, rawDepth, 1.0);
    float4 worldScene = mul(clipScene, invViewProj);
    worldScene /= worldScene.w;
    float sceneT = length(worldScene.xyz - cameraPos);
    
    if (rawDepth < 1.0) { 
        tExit = min(tExit, sceneT); 
    }
    
    if (tEntry >= tExit) return float4(0, 0, 0, 0); 
    
    // Longer grazing rays retain more samples; short upward rays need fewer.
    int steps = (int)clamp(ceil((tExit-tEntry)/14.0f),40.0f,128.0f);
    float marchDist = tExit - tEntry;
    float stepSize = marchDist / float(steps); 
    
    float transmittance = 1.0;
    float3 scatteredLight = float3(0, 0, 0);
    float currentT = tEntry + stepSize * CloudHash(float3(input.pos.xy,0.37f)); // Dither start pos to reduce banding
    
    float sunElevation = -lightDir.y;
    float dayFactor = saturate(sunElevation * 2.5f);
    float sunsetFactor = (sunElevation > -0.02f && sunElevation < 0.35f) ? exp(-pow((sunElevation - 0.08f) * 10.0f, 2.0f)) : 0.0f;
    float nightLightScale = saturate(1.0f - saturate((-sunElevation - 0.10f) / 0.35f));
    float stormFactor = saturate((cloudDensity - 0.85f) / 1.80f);
    
    float2 sunHorizontal=(-lightDir.xz)/max(length(lightDir.xz),.001f);
    float2 viewHorizontal=rayDir.xz/max(length(rayDir.xz),.001f);
    float sunSide=pow(saturate(dot(sunHorizontal,viewHorizontal)*.5f+.5f),1.5f);
    // Day sun vs Night moonlight (fading to true black when light is gone)
    float3 daySunColor = float3(1.0f, 0.95f, 0.90f) * 1.15f;
    float3 moonColor = float3(0.04f, 0.06f, 0.12f) * nightLightScale;
    float3 sunColor = lerp(moonColor, daySunColor, dayFactor) + float3(1.0f, 0.35f, 0.08f) * (sunsetFactor * 1.7f * lerp(.10f,1.0f,sunSide));
    
    // Ambient cloud illumination: clear day soft blue-white vs dark stormy slate-gray
    float3 dayAmbient = float3(0.50f, 0.60f, 0.80f) * 0.55f;
    float3 stormAmbient = float3(0.16f, 0.19f, 0.26f) * (dayFactor * 0.8f + 0.2f);
    float3 activeDayAmbient = lerp(dayAmbient, stormAmbient, stormFactor * 0.88f);
    
    float3 nightAmbient = float3(0.002f, 0.004f, 0.008f) * nightLightScale;
    float3 ambientColor = lerp(nightAmbient, activeDayAmbient, dayFactor);
    ambientColor=lerp(ambientColor,float3(.025f,.035f,.065f)*lerp(.5f,1.5f,sunSide),sunsetFactor*.85f);
    
    // Sun attenuation through storm clouds
    sunColor = lerp(sunColor, sunColor * 0.18f, stormFactor);
    
    float3 lightDirNorm = normalize(-lightDir);
    
    // Silver lining phase function (Henyey-Greenstein forward scattering)
    float cosAngle = dot(rayDir, lightDirNorm);
    float g = 0.6f;
    float hgPhase = (1.0f - g * g) / pow(max(1.0f + g * g - 2.0f * g * cosAngle, 0.01f), 1.5f) * (1.0f / (4.0f * 3.14159f));
    float phase = min(2.0f,lerp(0.65f, hgPhase * 3.14159f * 2.0f, 0.55f));
    
    if (cloudDensity <= 0.001f) return float4(0, 0, 0, 0);

    for (int i = 0; i < steps; i++) {
        if (currentT >= tExit || transmittance <= 0.01f) break;
        
        float3 pos = cameraPos + rayDir * currentT;
        float d = SampleCloudDensity(pos, time, cloudDensity);
        
        if (d > 0.01f) {
            // Integrate extinction toward the sun rather than sampling one neighbour.
            float opticalDepth=0;
            float shadowDistances[3]={36,144,360};
            float shadowWidths[3]={72,144,288};
            [unroll] for(int lightStep=0;lightStep<3;++lightStep)
                opticalDepth+=SampleCloudBody(pos+lightDirNorm*shadowDistances[lightStep],time,cloudDensity)*shadowWidths[lightStep]*CloudExtinction;
            float direct=exp(-opticalDepth);
            float multipleScatter=.18*exp(-opticalDepth*.25);
            float height=saturate((pos.y-layer.x)/(layer.y-layer.x));
            float powder=1-exp(-d*2);
            float3 ambient=ambientColor*lerp(.45,1.25,height);
            float3 particleLight=ambient+sunColor*(direct*phase+multipleScatter)*(0.65+0.35*powder);

            float stepTransmittance = exp(-d * stepSize * CloudExtinction);
            float stepOpacity = 1.0f - stepTransmittance;
            
            scatteredLight += particleLight * stepOpacity * transmittance;
            transmittance *= stepTransmittance;
            currentT += stepSize;
        } else {
            // Empty air space skipping: advance faster
            currentT += stepSize;
        }
    }
    
    float opacity = 1.0 - transmittance;
    
    // Fade out clouds as they approach the horizon so we don't see the sharp cutoff
    float horizonFade = smoothstep(0.005, 0.08, rayDir.y);
    opacity *= horizonFade;
    
    return float4(scatteredLight * horizonFade, opacity);
}
