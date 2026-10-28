#ifndef ENGINE_PBR_COMMON
#define ENGINE_PBR_COMMON
float2 ProjectedMeshUV(float3 position, float3 normal) {
    float3 n=abs(normal);
    if(n.y>=n.x && n.y>=n.z) return position.xz;
    if(n.x>=n.z) return position.zy;
    return position.xy;
}
static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
    
    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return min(nom / max(denom, 0.0001f), 100.0f);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 MaterialF0(float3 albedo,float metallic,float reflectance){return lerp((.16f*saturate(reflectance)*saturate(reflectance)).xxx,albedo,saturate(metallic));}
float3 EvaluateDirectBRDF(float3 N,float3 V,float3 L,float3 albedo,float roughness,float metallic,float reflectance){
    float nl=saturate(dot(N,L));if(nl<=0)return 0;
    float3 H=normalize(V+L);
    float3 F=fresnelSchlick(saturate(dot(H,V)),MaterialF0(albedo,metallic,reflectance));
    float3 spec=DistributionGGX(N,H,max(roughness,.045f))*GeometrySmith(N,V,L,roughness)*F;
    spec=clamp(spec/max(4*saturate(dot(N,V))*nl,.001f),0,4);
    return ((1-F)*(1-saturate(metallic))*albedo/PI+spec)*nl;
}


#endif
