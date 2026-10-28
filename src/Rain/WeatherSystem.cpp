#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WeatherSystem.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace EngineRain {

WeatherSystem::WeatherSystem() {
    m_nextLightningTime = 6.0f + (static_cast<float>(rand()) / RAND_MAX) * 6.0f;
}

void WeatherSystem::SetPreset(WeatherPreset preset, RainSettings& settings) {
    m_currentPreset = preset;
    settings.weather = preset;

    switch (preset) {
        case WeatherPreset::Clear:
            settings.enabled = false;
            settings.intensity = 0.0f;
            settings.wind.speed = 1.5f;
            settings.wind.gustStrength = 1.0f;
            settings.lightningEnabled = false;
            settings.cameraDrops.enabled = false;

            m_targetCloudDensity = 0.20f;
            m_targetFogDensity = 0.005f;
            m_targetSkyColor = { 0.35f, 0.50f, 0.75f };
            m_targetLightColor = { 1.0f, 0.98f, 0.95f };
            m_targetSunElevation = 45.0f;
            m_targetSunAzimuth = 45.0f;
            break;

        case WeatherPreset::LightRain:
            settings.enabled = true;
            settings.intensity = 0.35f;
            settings.particleCount = 2500;
            settings.fallSpeed = 18.0f;
            settings.dropLength = 0.30f;
            settings.dropWidth = 0.012f;
            settings.areaRadius = 24.0f;
            settings.areaHeight = 18.0f;
            settings.quality = RainQuality::Low;
            settings.wind.speed = 3.0f;
            settings.wind.gustStrength = 2.0f;
            settings.surface.enabled = true;
            settings.surface.wetnessEnabled = true;
            settings.surface.wetnessAccumRate = 0.12f;
            settings.surface.wetnessDryRate = 0.05f;
            settings.surface.puddlesEnabled = true;
            settings.surface.puddleAccumRate = 0.08f;
            settings.surface.puddleDryRate = 0.08f;
            settings.surface.splashesEnabled = true;
            settings.surface.splashIntensity = 0.40f;
            settings.surface.surfaceFlowEnabled = true;
            settings.surface.flowSpeed = 0.8f;
            settings.lightningEnabled = false;
            settings.cameraDrops.enabled = false;

            m_targetCloudDensity = 2.0f;
            m_targetFogDensity = 0.018f;
            m_targetSkyColor = { 0.28f, 0.34f, 0.44f };
            m_targetLightColor = { 0.65f, 0.68f, 0.74f };
            m_targetSunElevation = 35.0f;
            m_targetSunAzimuth = 45.0f;
            break;

        case WeatherPreset::Rain:
            settings.enabled = true;
            settings.intensity = 0.75f;
            settings.particleCount = 7500;
            settings.fallSpeed = 24.0f;
            settings.dropLength = 0.38f;
            settings.dropWidth = 0.016f;
            settings.areaRadius = 26.0f;
            settings.areaHeight = 20.0f;
            settings.quality = RainQuality::Medium;
            settings.wind.speed = 5.5f;
            settings.wind.gustStrength = 3.5f;
            settings.surface.enabled = true;
            settings.surface.wetnessEnabled = true;
            settings.surface.wetnessAccumRate = 0.18f;
            settings.surface.wetnessDryRate = 0.05f;
            settings.surface.puddlesEnabled = true;
            settings.surface.puddleAccumRate = 0.14f;
            settings.surface.puddleDryRate = 0.08f;
            settings.surface.splashesEnabled = true;
            settings.surface.splashIntensity = 0.65f;
            settings.surface.surfaceFlowEnabled = true;
            settings.surface.flowSpeed = 1.2f;
            settings.lightningEnabled = false;
            settings.cameraDrops.enabled = true;
            settings.cameraDrops.intensity = 0.40f;

            m_targetCloudDensity = 2.80f;
            m_targetFogDensity = 0.030f;
            m_targetSkyColor = { 0.18f, 0.22f, 0.30f };
            m_targetLightColor = { 0.40f, 0.44f, 0.52f };
            m_targetSunElevation = 24.0f;
            m_targetSunAzimuth = 45.0f;
            break;

        case WeatherPreset::HeavyRain:
            settings.enabled = true;
            settings.intensity = 1.20f;
            settings.particleCount = 15000;
            settings.fallSpeed = 28.0f;
            settings.dropLength = 0.48f;
            settings.dropWidth = 0.020f;
            settings.areaRadius = 28.0f;
            settings.areaHeight = 22.0f;
            settings.quality = RainQuality::High;
            settings.wind.speed = 10.0f;
            settings.wind.gustStrength = 6.0f;
            settings.surface.enabled = true;
            settings.surface.wetnessEnabled = true;
            settings.surface.wetnessAccumRate = 0.25f;
            settings.surface.wetnessDryRate = 0.05f;
            settings.surface.puddlesEnabled = true;
            settings.surface.puddleAccumRate = 0.20f;
            settings.surface.puddleDryRate = 0.08f;
            settings.surface.splashesEnabled = true;
            settings.surface.splashIntensity = 0.90f;
            settings.surface.surfaceFlowEnabled = true;
            settings.surface.flowSpeed = 1.5f;
            settings.lightningEnabled = false;
            settings.cameraDrops.enabled = true;
            settings.cameraDrops.intensity = 0.65f;

            m_targetCloudDensity = 3.40f;
            m_targetFogDensity = 0.045f;
            m_targetSkyColor = { 0.12f, 0.14f, 0.20f };
            m_targetLightColor = { 0.25f, 0.28f, 0.35f };
            m_targetSunElevation = 18.0f;
            m_targetSunAzimuth = 45.0f;
            break;

        case WeatherPreset::Storm:
            settings.enabled = true;
            settings.intensity = 1.60f;
            settings.particleCount = 25000;
            settings.fallSpeed = 34.0f;
            settings.dropLength = 0.58f;
            settings.dropWidth = 0.024f;
            settings.areaRadius = 30.0f;
            settings.areaHeight = 24.0f;
            settings.quality = RainQuality::Ultra;
            settings.wind.speed = 16.0f;
            settings.wind.gustStrength = 10.0f;
            settings.surface.enabled = true;
            settings.surface.wetnessEnabled = true;
            settings.surface.wetnessAccumRate = 0.35f;
            settings.surface.wetnessDryRate = 0.05f;
            settings.surface.puddlesEnabled = true;
            settings.surface.puddleAccumRate = 0.28f;
            settings.surface.puddleDryRate = 0.08f;
            settings.surface.splashesEnabled = true;
            settings.surface.splashIntensity = 1.20f;
            settings.surface.surfaceFlowEnabled = true;
            settings.surface.flowSpeed = 1.8f;
            settings.lightningEnabled = true;
            settings.lightningFrequency = 0.25f;
            settings.cameraDrops.enabled = true;
            settings.cameraDrops.intensity = 0.85f;

            m_targetCloudDensity = 3.90f;
            m_targetFogDensity = 0.058f;
            m_targetSkyColor = { 0.06f, 0.07f, 0.11f };
            m_targetLightColor = { 0.14f, 0.16f, 0.22f };
            m_targetSunElevation = 14.0f;
            m_targetSunAzimuth = 45.0f;
            break;
    }
}

void WeatherSystem::Update(float dt, RainSettings& settings) {
    // 1. Atmospheric smooth interpolation
    float blendRate = (std::clamp)(dt * 1.8f, 0.0f, 1.0f);
    m_cloudDensity = m_cloudDensity + (m_targetCloudDensity - m_cloudDensity) * blendRate;
    m_fogDensity = m_fogDensity + (m_targetFogDensity - m_fogDensity) * blendRate;
    m_sunElevation = m_sunElevation + (m_targetSunElevation - m_sunElevation) * blendRate;
    m_sunAzimuth = m_sunAzimuth + (m_targetSunAzimuth - m_sunAzimuth) * blendRate;

    m_skyColor.x = m_skyColor.x + (m_targetSkyColor.x - m_skyColor.x) * blendRate;
    m_skyColor.y = m_skyColor.y + (m_targetSkyColor.y - m_skyColor.y) * blendRate;
    m_skyColor.z = m_skyColor.z + (m_targetSkyColor.z - m_skyColor.z) * blendRate;

    m_lightColor.x = m_lightColor.x + (m_targetLightColor.x - m_lightColor.x) * blendRate;
    m_lightColor.y = m_lightColor.y + (m_targetLightColor.y - m_lightColor.y) * blendRate;
    m_lightColor.z = m_lightColor.z + (m_targetLightColor.z - m_lightColor.z) * blendRate;

    // 2. Lightning simulation during storm
    if (settings.enabled && settings.lightningEnabled && settings.weather == WeatherPreset::Storm) {
        m_lightningTimer += dt;

        if (m_lightningTimer >= m_nextLightningTime) {
            // Trigger new lightning strike
            m_lightningTimer = 0.0f;
            float interval = 3.5f + (1.0f - settings.lightningFrequency) * 6.0f;
            m_nextLightningTime = interval + (static_cast<float>(rand()) / RAND_MAX) * 3.5f;
            m_flashSubCount = 2 + (rand() % 3); // 2 to 4 flashes per strike
            m_flashSubTimer = 0.0f;
            m_lightningIntensity = 1.0f;
        }

        // Sub-flash pulsing & rapid decay
        if (m_flashSubCount > 0) {
            m_flashSubTimer += dt;
            if (m_flashSubTimer > 0.08f) {
                m_flashSubTimer = 0.0f;
                m_flashSubCount--;
                if (m_flashSubCount > 0) {
                    m_lightningIntensity = 0.6f + (static_cast<float>(rand()) / RAND_MAX) * 0.4f;
                }
            }
        }

        // Exponential decay
        m_lightningIntensity = (std::max)(0.0f, m_lightningIntensity - dt * 5.0f);
    } else {
        m_lightningIntensity = 0.0f;
    }
}

} // namespace EngineRain
