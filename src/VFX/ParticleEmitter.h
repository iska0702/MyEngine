#pragma once

#include "ParticleTypes.h"
#include <vector>
#include <string>
#include <memory>

namespace EngineVFX {

struct ParticleEmitterConfig {
    std::string name = "ParticleEmitter";
    EmitterType type = EmitterType::Point;
    BlendMode blendMode = BlendMode::Additive;

    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 direction = { 0.0f, 1.0f, 0.0f }; // Primary emission heading
    XMFLOAT3 boxExtents = { 1.0f, 1.0f, 1.0f };

    float spawnRate = 40.0f;       // Particles per second
    size_t maxParticles = 400;     // Pool capacity
    float minLifetime = 0.6f;
    float maxLifetime = 1.4f;

    float minSpeed = 1.5f;
    float maxSpeed = 4.0f;
    float coneAngle = 35.0f;       // Degrees of spread

    float startSize = 0.15f;
    float endSize = 0.6f;

    XMFLOAT4 startColor = { 1.0f, 0.6f, 0.1f, 1.0f };
    XMFLOAT4 endColor   = { 1.0f, 0.1f, 0.0f, 0.0f };

    XMFLOAT3 gravity = { 0.0f, -2.5f, 0.0f };
    float drag = 0.4f;

    float minRotSpeed = -2.0f;
    float maxRotSpeed =  2.0f;

    bool looping = true;
    float duration = 5.0f;
};

class ParticleEmitter {
public:
    ParticleEmitter(const ParticleEmitterConfig& config);
    ~ParticleEmitter();

    void Update(float dt, const XMFLOAT3& cameraPos);
    void Burst(size_t count);

    void GenerateVertices(const XMFLOAT3& cameraPos, const XMFLOAT3& cameraUp,
                          std::vector<ParticleVertex>& outVertices,
                          std::vector<uint32_t>& outIndices) const;

    bool IsFinished() const;
    void Reset();

    // Getters / Setters
    const ParticleEmitterConfig& GetConfig() const { return m_config; }
    ParticleEmitterConfig& GetConfig() { return m_config; }

    const XMFLOAT3& GetPosition() const { return m_config.position; }
    void SetPosition(const XMFLOAT3& pos) { m_config.position = pos; }

    BlendMode GetBlendMode() const { return m_config.blendMode; }
    void SetBlendMode(BlendMode mode) { m_config.blendMode = mode; }

    bool IsActive() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    size_t GetAliveCount() const { return m_aliveCount; }
    size_t GetTotalSpawned() const { return m_totalSpawned; }

private:
    void SpawnParticle();
    XMFLOAT3 CalculateSpawnVelocity() const;

    ParticleEmitterConfig m_config;
    std::vector<Particle> m_particles;
    size_t m_aliveCount = 0;
    size_t m_totalSpawned = 0;

    float m_spawnTimer = 0.0f;
    float m_age = 0.0f;
    bool m_active = true;
};

} // namespace EngineVFX
