#pragma once
#include <algorithm>
#include <cmath>

namespace EngineRain {
// Fade only the particles; never move or draw a ground plane with the rain volume.
inline float RainVolumeOpacity(float x, float z, float cameraX, float cameraZ, float radius) {
    if (radius <= 0.0f) return 0.0f;
    float dx = x - cameraX, dz = z - cameraZ;
    float distance = std::sqrt(dx * dx + dz * dz) / radius;
    float t = (std::clamp)((distance - 0.65f) / 0.35f, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}
}
