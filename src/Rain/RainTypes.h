#pragma once
#include <DirectXMath.h>
#include <string>

namespace EngineRain {

using namespace DirectX;

enum class RainQuality {
    Low = 0,     // 5,000 particles
    Medium = 1,  // 15,000 particles
    High = 2,    // 35,000 particles
    Ultra = 3    // 70,000 particles
};

enum class WeatherPreset {
    Clear = 0,
    LightRain = 1,
    Rain = 2,
    HeavyRain = 3,
    Storm = 4
};

struct WindSettings {
    bool enabled = true;
    XMFLOAT3 direction = { 0.2f, -1.0f, 0.1f };
    float speed = 5.0f;
    float gustStrength = 3.5f;
    float gustFrequency = 0.4f;
};

struct SurfaceInteractionSettings {
    bool enabled = true; // Master surface interaction switch

    bool wetnessEnabled = true;
    float wetnessStrength = 1.0f;
    float wetness = 0.0f; // Dynamic 0.0 to 1.0 (accumulates with rain, dries when clear)
    float wetnessAccumRate = 0.15f;
    float wetnessDryRate = 0.05f;

    bool puddlesEnabled = true;
    float puddleAccumulation = 1.0f;
    float puddleAmount = 0.0f; // Dynamic 0.0 to 1.0 (accumulates with rain, dries when clear)
    float puddleAccumRate = 0.10f;
    float puddleDryRate = 0.08f;

    bool splashesEnabled = true;
    float splashIntensity = 0.65f;

    bool surfaceFlowEnabled = true;
    float trailStrength = 0.8f;
    float flowSpeed = 1.2f;
};

struct CameraDropletSettings {
    bool enabled = false;
    float intensity = 0.5f;
    float distortion = 0.6f;
};

struct DebugVisSettings {
    bool showParticles = true;
    bool showCollision = false;
    bool showNormals = false;
    bool showFlow = false;
    bool showWetness = false;
    bool showPuddles = false;
};

struct RainSettings {
    bool enabled = false;
    float intensity = 0.75f;
    int particleCount = 7500;
    bool useCustomParticleCount = true;

    float fallSpeed = 24.0f;
    float dropLength = 0.38f;
    float dropWidth = 0.016f;
    float areaRadius = 26.0f;
    float areaHeight = 20.0f;

    RainQuality quality = RainQuality::High;
    WeatherPreset weather = WeatherPreset::Clear;

    WindSettings wind;
    SurfaceInteractionSettings surface;
    CameraDropletSettings cameraDrops;
    DebugVisSettings debugVis;

    bool lightningEnabled = true;
    float lightningFrequency = 0.15f; // Chance per second during storm
};

struct RainDebugStats {
    int activeParticles = 0;
    int activeSplashes = 0;
    int collisionHitsPerSec = 0;
    float simulationTimeMs = 0.0f;
    float renderTimeMs = 0.0f;
    float currentWetness = 0.0f;
    float currentPuddle = 0.0f;
    XMFLOAT3 currentWind = { 0, 0, 0 };
    bool isLightningActive = false;
};

inline int GetParticleCountForQuality(RainQuality quality) {
    switch (quality) {
        case RainQuality::Low:    return 1500;
        case RainQuality::Medium: return 4000;
        case RainQuality::High:   return 8000;
        case RainQuality::Ultra:  return 16000;
        default:                  return 7500;
    }
}

} // namespace EngineRain
