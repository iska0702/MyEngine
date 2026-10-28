#include <metal_stdlib>
using namespace metal;

kernel void CSMain(uint2 id [[thread_position_in_grid]],
                   texture2d<float, access::read> inDepth [[texture(0)]],
                   texture2d<float, access::write> outDepth [[texture(1)]]) {
    if (id.x >= outDepth.get_width() || id.y >= outDepth.get_height()) {
        return;
    }

    uint2 srcCoord = id * 2;
    float d0 = inDepth.read(srcCoord).r;
    float d1 = inDepth.read(srcCoord + uint2(1, 0)).r;
    float d2 = inDepth.read(srcCoord + uint2(0, 1)).r;
    float d3 = inDepth.read(srcCoord + uint2(1, 1)).r;

    float minDepth = min(min(d0, d1), min(d2, d3));
    outDepth.write(minDepth, id);
}
