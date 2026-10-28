Texture2D<float> InDepth : register(t0);
RWTexture2D<float> OutHiZ : register(u0);
cbuffer HiZConstants : register(b0){uint2 OutputSize;};
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=OutputSize))return;
    uint width,height;InDepth.GetDimensions(width,height);
    uint2 inputSize=uint2(width,height);
    uint2 begin=(id.xy*inputSize)/OutputSize;
    uint2 end=((id.xy+1)*inputSize)/OutputSize;
    float depth=0;
    // Includes every source texel for odd-sized mip levels. Equal dimensions
    // copy the depth buffer into mip zero; smaller levels store farthest depth.
    for(uint y=begin.y;y<end.y;++y)for(uint x=begin.x;x<end.x;++x)
        depth=max(depth,InDepth.Load(int3(x,y,0)));
    OutHiZ[id.xy]=depth;
}
