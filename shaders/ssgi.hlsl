// Cosine-weighted, thickness-tested screen-space diffuse bounce.
Texture2D<float4> g_NormalTex : register(t0);
Texture2D<float> g_DepthTex : register(t1);
Texture2D<float4> g_RadianceTex : register(t2);
Texture2D<float4> g_DiffuseAlbedo : register(t3);
RWTexture2D<float4> g_OutputTex : register(u0);
SamplerState g_Sampler : register(s0);
cbuffer SSGIConstants : register(b0){
    matrix InvViewProj;matrix ViewProj;
    float3 CameraPos;float RayStepSize;
    float MaxRayDistance;int RaySteps;float Intensity;float Padding;
}
float3 WorldPosition(float2 uv,float depth){float4 p=mul(float4(uv*float2(2,-2)+float2(-1,1),depth,1),InvViewProj);return p.xyz/p.w;}
float Random(float2 pixel){return frac(52.9829189f*frac(dot(pixel,float2(.06711056f,.00583715f))));}
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID){
    uint width,height;g_OutputTex.GetDimensions(width,height);
    if(id.x>=width||id.y>=height)return;
    float2 uv=(float2(id.xy)+.5f)/float2(width,height);
    float depth=g_DepthTex.Load(int3(id.xy,0));
    float3 normal=g_NormalTex.Load(int3(id.xy,0)).xyz;
    float3 diffuse=g_DiffuseAlbedo.Load(int3(id.xy,0)).rgb;
    if(depth>=.999999f||dot(normal,normal)<.1f||!all(isfinite(normal))||max(diffuse.r,max(diffuse.g,diffuse.b))<.001f){g_OutputTex[id.xy]=0;return;}
    normal=normalize(normal);float3 origin=WorldPosition(uv,depth)+normal*.025f;
    float3 tangent=normalize(cross(abs(normal.z)<.99f?float3(0,0,1):float3(0,1,0),normal));
    float3 bitangent=cross(normal,tangent),incoming=0;
    float rotation=Random(id.xy)*6.2831853f;
    [unroll]for(int sample=0;sample<2;++sample){
        float r=(sample+.5f)/2.f,phi=rotation+sample*2.3999632f;
        float3 direction=tangent*(cos(phi)*sqrt(r))+bitangent*(sin(phi)*sqrt(r))+normal*sqrt(1-r);
        float previousDistance=RayStepSize;
        [loop]for(int step=0;step<RaySteps;++step){
            float t=(step+1.f)/RaySteps;
            float distance=RayStepSize+(MaxRayDistance-RayStepSize)*t*t;
            float low=previousDistance;previousDistance=distance;
            float3 current=origin+direction*distance;
            float4 clip=mul(float4(current,1),ViewProj);
            if(clip.w<=.001f)break;
            float3 ndc=clip.xyz/clip.w;float2 sampleUV=ndc.xy*float2(.5f,-.5f)+.5f;
            if(any(sampleUV<=0)||any(sampleUV>=1)||ndc.z<=0||ndc.z>=1)break;
            float hitDepth=g_DepthTex.SampleLevel(g_Sampler,sampleUV,0);
            if(hitDepth>=.999999f||ndc.z<=hitDepth)continue;
            // Refine the first depth crossing instead of missing a thin surface
            // whenever a coarse world-space step overshoots its thickness.
            float high=distance;
            [unroll]for(int refine=0;refine<5;++refine){
                float mid=(low+high)*.5f;
                float4 probe=mul(float4(origin+direction*mid,1),ViewProj);
                if(probe.w<=.001f)break;
                float3 projected=probe.xyz/probe.w;
                float2 probeUV=projected.xy*float2(.5f,-.5f)+.5f;
                if(any(probeUV<=0)||any(probeUV>=1))break;
                float probeDepth=g_DepthTex.SampleLevel(g_Sampler,probeUV,0);
                if(projected.z>probeDepth&&probeDepth<.999999f){high=mid;sampleUV=probeUV;}else low=mid;
            }
            current=origin+direction*high;
            hitDepth=g_DepthTex.SampleLevel(g_Sampler,sampleUV,0);
            float3 hit=WorldPosition(sampleUV,hitDepth);
            float thickness=.08f+distance*.025f;
            if(length(hit-current)>thickness)continue;
            float3 hitNormal=g_NormalTex.SampleLevel(g_Sampler,sampleUV,0).xyz;
            if(dot(hitNormal,hitNormal)<.1f)break;
            float facing=saturate(dot(-direction,normalize(hitNormal)));
            float edge=saturate(min(min(sampleUV.x,1-sampleUV.x),min(sampleUV.y,1-sampleUV.y))*20);
            incoming+=max(g_RadianceTex.SampleLevel(g_Sampler,sampleUV,0).rgb,0)*facing*edge;
            break;
        }
    }
    float3 result=diffuse*(incoming*.5f)*Intensity;
    g_OutputTex[id.xy]=float4(all(isfinite(result))?min(result,8.f):0,1);
}
