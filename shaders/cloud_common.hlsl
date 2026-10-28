#ifndef ENGINE_CLOUD_COMMON
#define ENGINE_CLOUD_COMMON

float CloudHash(float3 p) {
    p = frac(p * 0.1031f);
    p += dot(p, p.zyx + 31.32f);
    return frac((p.x + p.y) * p.z);
}

float CloudNoise(float3 x) {
    float3 p = floor(x), f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(CloudHash(p), CloudHash(p + float3(1,0,0)), f.x),
                          lerp(CloudHash(p + float3(0,1,0)), CloudHash(p + float3(1,1,0)), f.x), f.y),
                lerp(lerp(CloudHash(p + float3(0,0,1)), CloudHash(p + float3(1,0,1)), f.x),
                     lerp(CloudHash(p + float3(0,1,1)), CloudHash(p + float3(1,1,1)), f.x), f.y), f.z);
}

float CloudFbm(float3 p) {
    float result = 0.0f, weight = 0.5f;
    [unroll] for (int i = 0; i < 5; ++i) {
        result += weight * CloudNoise(p);
        p *= 2.0f;
        weight *= 0.5f;
    }
    return result;
}

float2 CloudLayerBounds(float density) {
    float storm = saturate((density - 0.85f) / 1.80f);
    float base = lerp(1400.0f, 1000.0f, storm);
    return float2(base, base + lerp(650.0f, 900.0f, storm));
}

// Shared by visible clouds and surface lighting: same shape, wind and extinction.
static const float CloudExtinction = 0.0075f;
// Weather-scale coverage and height profile are shared with raster/DXR shadows.
float SampleCloudBody(float3 p, float time, float density) {
    float2 layer=CloudLayerBounds(density);
    float h=(p.y-layer.x)/(layer.y-layer.x);
    if(h<=0 || h>=1 || density<=0) return 0;
    float storm=saturate((density-.85)/1.8);
    p.xz+=time*float2(1.5+storm*3.5,.6+storm*1.4);
    float weather=CloudNoise(float3(p.x*.00028,0,p.z*.00028));
    float coverage=lerp(.46,.83,storm)+(weather-.5)*.32;
    float3 q=p*.0017;
    // Rounded billows over broad, flatter condensation bases.
    float billow=CloudNoise(q)*.65+CloudNoise(q*2.03+13.7)*.35;
    float shape=saturate((billow-(1-coverage))*3.5);
    float top=lerp(.52,.98,saturate(weather*1.3));
    float profile=smoothstep(0,.08,h)*(1-smoothstep(top*.55,top,h));
    return shape*profile*density*1.8;
}
float SampleCloudDensity(float3 p, float time, float density) {
    float body=SampleCloudBody(p,time,density);
    if(body<=.001) return 0;
    float storm=saturate((density-.85)/1.8);
    float3 q=p+float3(time*(1.5+storm*3.5),0,time*(.6+storm*1.4));
    float detail=CloudNoise(q*.009)*.65+CloudNoise(q*.021+27.3)*.35;
    // Fine erosion is strongest at the silhouette, keeping dense interiors intact.
    float edge=1-saturate(body/max(density,.01));
    return max(0,body-(1-detail)*edge*.32*density);
}

float GetCloudShadow(float3 worldPos, float3 lightDir, float time, float density) {
    if (density <= 0.001f || lightDir.y >= -0.01f) return 1.0f;
    float3 sunRay = normalize(-lightDir);
    float2 layer = CloudLayerBounds(density);
    float entry = max((layer.x - worldPos.y) / sunRay.y, 0.0f);
    float exit = max((layer.y - worldPos.y) / sunRay.y, 0.0f);
    if (exit <= entry) return 1.0f;
    const int steps = 8;
    float stepSize = (exit - entry) / steps;
    float opticalDepth = 0.0f;
    [unroll] for (int i = 0; i < steps; ++i) {
        float3 p = worldPos + sunRay * (entry + (i + 0.5f) * stepSize);
        opticalDepth += SampleCloudBody(p, time, density) * stepSize * CloudExtinction;
    }
    return exp(-opticalDepth);
}
#endif
