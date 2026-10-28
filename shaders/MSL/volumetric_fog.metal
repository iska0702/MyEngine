#include <metal_stdlib>
using namespace metal;

struct FogConstants {
    float4x4 invViewProj;
    float4   cameraPosAndTime;
    float4   lightDir;
    float4   lightColor;
    float4   fogParams; // x = density, y = anisotropy, z = near, w = far
};

kernel void InjectLightCS(uint3 id [[thread_position_in_grid]],
                          constant FogConstants& cb [[buffer(0)]],
                          texture3d<float, access::write> volumeOut [[texture(0)]]) {
    if (id.x >= volumeOut.get_width() || id.y >= volumeOut.get_height() || id.z >= volumeOut.get_depth()) {
        return;
    }

    float3 uvw = (float3(id) + 0.5f) / float3(volumeOut.get_width(), volumeOut.get_height(), volumeOut.get_depth());
    float density = cb.fogParams.x * exp(-uvw.y * 3.0f);

    float3 lightDir = normalize(cb.lightDir.xyz);
    float scattering = density * (0.8f + 0.2f * max(dot(-lightDir, float3(0, 0, 1)), 0.0f));
    float3 inscatter = cb.lightColor.rgb * scattering;

    volumeOut.write(float4(inscatter, density), id);
}

kernel void AccumulateFogCS(uint2 id [[thread_position_in_grid]],
                            constant FogConstants& cb [[buffer(0)]],
                            texture3d<float, access::read> volumeIn [[texture(0)]],
                            texture3d<float, access::write> volumeAccum [[texture(1)]]) {
    if (id.x >= volumeAccum.get_width() || id.y >= volumeAccum.get_height()) {
        return;
    }

    float4 accum = float4(0.0f, 0.0f, 0.0f, 1.0f); // xyz = color, w = transmittance
    uint depth = volumeAccum.get_depth();

    for (uint z = 0; z < depth; ++z) {
        float4 slice = volumeIn.read(uint3(id.x, id.y, z));
        float stepSize = 1.0f / float(depth);
        float sliceTransmittance = exp(-slice.a * stepSize * 50.0f);

        accum.rgb += slice.rgb * accum.a * stepSize;
        accum.a *= sliceTransmittance;

        volumeAccum.write(accum, uint3(id.x, id.y, z));
    }
}
