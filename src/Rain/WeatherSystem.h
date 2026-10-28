#pragma once
#include "RainTypes.h"
#include <DirectXMath.h>

namespace EngineRain {

class WeatherSystem {
public:
    WeatherSystem();

    void SetPreset(WeatherPreset preset, RainSettings& settings);
    void Update(float dt, RainSettings& settings);

    float GetLightningFlash() const { return m_lightningIntensity; }
    XMFLOAT3 GetLightningColor() const { return XMFLOAT3(0.9f, 0.95f, 1.0f); }
    bool IsLightningActive() const { return m_lightningIntensity > 0.01f; }

    WeatherPreset GetCurrentPreset() const { return m_currentPreset; }

    // Dynamic Weather Atmosphere Properties
    float GetCloudDensity() const { return m_cloudDensity; }
    float GetFogDensity() const { return m_fogDensity; }
    XMFLOAT3 GetSkyColor() const { return m_skyColor; }
    XMFLOAT3 GetLightColor() const { return m_lightColor; }
    float GetSunElevation() const { return m_sunElevation; }
    float GetSunAzimuth() const { return m_sunAzimuth; }

private:
    WeatherPreset m_currentPreset = WeatherPreset::Clear;

    // Atmospheric properties
    float m_cloudDensity = 0.20f;
    float m_targetCloudDensity = 0.20f;

    float m_fogDensity = 0.008f;
    float m_targetFogDensity = 0.008f;

    XMFLOAT3 m_skyColor = { 0.35f, 0.50f, 0.75f };
    XMFLOAT3 m_targetSkyColor = { 0.35f, 0.50f, 0.75f };

    XMFLOAT3 m_lightColor = { 1.0f, 0.98f, 0.95f };
    XMFLOAT3 m_targetLightColor = { 1.0f, 0.98f, 0.95f };

    float m_sunElevation = 45.0f;
    float m_targetSunElevation = 45.0f;

    float m_sunAzimuth = 45.0f;
    float m_targetSunAzimuth = 45.0f;

    // Lightning state
    float m_lightningTimer = 0.0f;
    float m_nextLightningTime = 5.0f;
    float m_lightningIntensity = 0.0f;
    int m_flashSubCount = 0;
    float m_flashSubTimer = 0.0f;
};

} // namespace EngineRain
