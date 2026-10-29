#include "shaders.hlsl"
cbuffer TessellationConstants : register(b1) {
    float TessFactor;
    float TessSmoothing;
    float TessNear;
    float TessFar;
};
struct TessVertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
TessVertex TessVS(uint id : SV_VertexID) {
    uint index=g_Indices.Load((startIndexLocation+id)*4);
    Vertex v=g_Vertices[baseVertexLocation+index];
    TessVertex o;o.position=v.position;o.normal=v.normal;
    o.uv=padIndices!=0?v.color.zw:ProjectedMeshUV(v.position,v.normal);
    return o;
}
struct TessPatch {float edges[3]:SV_TessFactor;float inside:SV_InsideTessFactor;};
float EdgeFactor(float3 a,float3 b){
    float3 midpoint=mul(float4((a+b)*.5,1),Model).xyz;
    float fade=saturate((distance(CameraPos,midpoint)-TessNear)/max(TessFar-TessNear,.1));
    return lerp(clamp(TessFactor,1,16),1,fade);
}
TessPatch TessFactors(InputPatch<TessVertex,3> p){
    TessPatch f;
    f.edges[0]=EdgeFactor(p[1].position,p[2].position);
    f.edges[1]=EdgeFactor(p[2].position,p[0].position);
    f.edges[2]=EdgeFactor(p[0].position,p[1].position);
    f.inside=max(f.edges[0],max(f.edges[1],f.edges[2]));return f;
}
[domain("tri")][partitioning("fractional_odd")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("TessFactors")][maxtessfactor(16)]
TessVertex TessHS(InputPatch<TessVertex,3> p,uint id:SV_OutputControlPointID){return p[id];}
[domain("tri")]
PSInput TessDS(TessPatch f,float3 b:SV_DomainLocation,const OutputPatch<TessVertex,3> p){
    float3 linearPoint=p[0].position*b.x+p[1].position*b.y+p[2].position*b.z;
    // Phong tessellation: interpolate the projections onto vertex tangent planes.
    float3 n0=normalize(p[0].normal),n1=normalize(p[1].normal),n2=normalize(p[2].normal);
    float3 projected=linearPoint-(n0*dot(linearPoint-p[0].position,n0)*b.x+
        n1*dot(linearPoint-p[1].position,n1)*b.y+n2*dot(linearPoint-p[2].position,n2)*b.z);
    float3 position=lerp(linearPoint,projected,saturate(TessSmoothing));
    float3 normal=normalize(n0*b.x+n1*b.y+n2*b.z);
    float4 world=mul(float4(position,1),Model);
    float3x3 basis=(float3x3)Model;
    float3x3 cofactors=float3x3(cross(basis[1],basis[2]),cross(basis[2],basis[0]),cross(basis[0],basis[1]));
    PSInput o;o.position=mul(world,ViewProj);o.worldPos=world.xyz;
    o.normal=normalize(mul(normal,cofactors)*(dot(basis[0],cofactors[0])<0?-1:1));
    o.color=1;o.uv=p[0].uv*b.x+p[1].uv*b.y+p[2].uv*b.z;
    o.curPos=mul(world,CleanViewProj);o.prevPos=mul(mul(float4(position,1),PreviousModel),PrevCleanViewProj);return o;
}
