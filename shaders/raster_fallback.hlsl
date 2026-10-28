#include "shaders.hlsl"
PSInput RasterVS(uint id:SV_VertexID){
    uint index=g_Indices.Load((startIndexLocation+id)*4);
    Vertex v=g_Vertices[baseVertexLocation+index];
    float4 world=mul(float4(v.position,1),Model);
    float3x3 basis=(float3x3)Model;
    float3x3 cofactors=float3x3(cross(basis[1],basis[2]),cross(basis[2],basis[0]),cross(basis[0],basis[1]));
    PSInput o;o.position=mul(world,ViewProj);o.worldPos=world.xyz;
    o.normal=normalize(mul(v.normal,cofactors)*(dot(basis[0],cofactors[0])<0?-1:1));
    o.color=1;o.uv=padIndices!=0?v.color.zw:ProjectedMeshUV(v.position,v.normal);
    o.curPos=mul(world,CleanViewProj);o.prevPos=mul(world,PrevCleanViewProj);return o;
}
