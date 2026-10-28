#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

struct RaytracingConstants {
    float4 cameraPos;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 lightDir;
    float4 lightColor;
    int shadowsEnabled;
    int reflectionsEnabled;
    int pathTracingEnabled;
    int maxBounces;
    int frameIndex;
    int samplesPerPixel;
    float cloudDensity;
    float cloudTime;
    float rainWetness;
    float rainPuddles;
    float pad1;
    float pad2;
};

kernel void RaytracingKernel(uint2 id [[thread_position_in_grid]],
                             constant RaytracingConstants& cb [[buffer(0)]],
                             instance_acceleration_structure accel [[buffer(1)]],
                             texture2d<float, access::write> outColor [[texture(0)]]) {
    if (id.x >= outColor.get_width() || id.y >= outColor.get_height()) {
        return;
    }

    float2 dims = float2(outColor.get_width(), outColor.get_height());
    float2 uv = (float2(id) + 0.5f) / dims * 2.0f - 1.0f;
    float aspect = dims.x / dims.y;

    float3 rayOrigin = cb.cameraPos.xyz;
    float3 rayDir = normalize(cb.cameraForward.xyz +
                              cb.cameraRight.xyz * uv.x * aspect +
                              cb.cameraUp.xyz * (-uv.y));

    ray r;
    r.origin = rayOrigin;
    r.direction = rayDir;
    r.min_distance = 0.01f;
    r.max_distance = 1000.0f;

    intersector<triangle_data, instancing> i;
    typename intersector<triangle_data, instancing>::result_type hit;
    hit = i.intersect(r, accel);

    float4 color = float4(0.1f, 0.15f, 0.25f, 1.0f); // Default background sky

    if (hit.type != intersection_type::none) {
        float3 hitPos = rayOrigin + rayDir * hit.distance;

        // Shadow Ray towards Light
        if (cb.shadowsEnabled) {
            ray shadowRay;
            shadowRay.origin = hitPos + normalize(-cb.lightDir.xyz) * 0.02f;
            shadowRay.direction = normalize(-cb.lightDir.xyz);
            shadowRay.min_distance = 0.01f;
            shadowRay.max_distance = 500.0f;

            typename intersector<triangle_data, instancing>::result_type shadowHit;
            shadowHit = i.intersect(shadowRay, accel);

            if (shadowHit.type == intersection_type::none) {
                // Lit
                color = float4(cb.lightColor.rgb * 0.8f + float3(0.1f), 1.0f);
            } else {
                // Shadowed
                color = float4(float3(0.05f), 1.0f);
            }
        } else {
            color = float4(0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    outColor.write(color, id);
}
