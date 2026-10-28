#ifndef ENGINE_LIGHTING_TYPES
#define ENGINE_LIGHTING_TYPES
struct PointLightData {
    float3 position;
    float  range;
    float3 color;
    float  intensity;
};

struct SpotLightData {
    float3 position;
    float  range;
    float3 direction;
    float  intensity;
    float3 color;
    float  innerConeCos;
    float  outerConeCos;
    float  falloff;
    float  enabled;
    float  pad;
};


#endif
