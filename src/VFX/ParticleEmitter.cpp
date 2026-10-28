#include "ParticleEmitter.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace EngineVFX {

static float RandomFloat(float a, float b) {
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return a + r * (b - a);
}

ParticleEmitter::ParticleEmitter(const ParticleEmitterConfig& config)
    : m_config(config)
{
    m_particles.resize(m_config.maxParticles);
}

ParticleEmitter::~ParticleEmitter() = default;

void ParticleEmitter::Reset() {
    m_age = 0.0f;
    m_spawnTimer = 0.0f;
    m_aliveCount = 0;
    for (auto& p : m_particles) p.alive = false;
}

bool ParticleEmitter::IsFinished() const {
    if (m_config.looping) return false;
    return (m_age >= m_config.duration) && (m_aliveCount == 0);
}

void ParticleEmitter::Burst(size_t count) {
    for (size_t i = 0; i < count; ++i) {
        SpawnParticle();
    }
}

XMFLOAT3 ParticleEmitter::CalculateSpawnVelocity() const {
    XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&m_config.direction));
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 0.001f) {
        dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    }

    // Cone perturbation
    float maxAngleRad = XMConvertToRadians(m_config.coneAngle);
    float angle = RandomFloat(0.0f, maxAngleRad);
    float phi = RandomFloat(0.0f, 6.2831853f);

    XMVECTOR arbitrary = (std::abs(XMVectorGetY(dir)) < 0.9f) ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR tangent = XMVector3Normalize(XMVector3Cross(dir, arbitrary));
    XMVECTOR bitangent = XMVector3Cross(dir, tangent);

    XMVECTOR perturbed = XMVector3Normalize(
        dir * std::cos(angle) +
        (tangent * std::cos(phi) + bitangent * std::sin(phi)) * std::sin(angle)
    );

    float speed = RandomFloat(m_config.minSpeed, m_config.maxSpeed);
    XMVECTOR vel = perturbed * speed;

    XMFLOAT3 result;
    XMStoreFloat3(&result, vel);
    return result;
}

void ParticleEmitter::SpawnParticle() {
    // Find dead particle
    Particle* target = nullptr;
    for (auto& p : m_particles) {
        if (!p.alive) {
            target = &p;
            break;
        }
    }
    if (!target) {
        // Pool full - replace first particle
        target = &m_particles[m_totalSpawned % m_particles.size()];
    } else {
        m_aliveCount++;
    }

    target->alive = true;
    target->age = 0.0f;
    target->lifetime = RandomFloat(m_config.minLifetime, m_config.maxLifetime);

    // Initial Position
    target->position = m_config.position;
    if (m_config.type == EmitterType::Sphere) {
        float theta = RandomFloat(0.0f, 6.2831853f);
        float phi = RandomFloat(0.0f, 3.14159265f);
        float r = RandomFloat(0.0f, m_config.boxExtents.x);
        target->position.x += r * std::sin(phi) * std::cos(theta);
        target->position.y += r * std::sin(phi) * std::sin(theta);
        target->position.z += r * std::cos(phi);
    } else if (m_config.type == EmitterType::Box) {
        target->position.x += RandomFloat(-m_config.boxExtents.x, m_config.boxExtents.x);
        target->position.y += RandomFloat(-m_config.boxExtents.y, m_config.boxExtents.y);
        target->position.z += RandomFloat(-m_config.boxExtents.z, m_config.boxExtents.z);
    }

    target->velocity = CalculateSpawnVelocity();
    target->acceleration = XMFLOAT3(0.0f, 0.0f, 0.0f);

    target->startSize = m_config.startSize;
    target->endSize   = m_config.endSize;
    target->size      = m_config.startSize;

    target->startColor   = m_config.startColor;
    target->endColor     = m_config.endColor;
    target->currentColor = m_config.startColor;

    target->rotation = RandomFloat(0.0f, 6.2831853f);
    target->rotationSpeed = RandomFloat(m_config.minRotSpeed, m_config.maxRotSpeed);

    m_totalSpawned++;
}

void ParticleEmitter::Update(float dt, const XMFLOAT3& cameraPos) {
    if (!m_active) return;

    m_age += dt;

    // Spawning logic
    if (m_config.looping || m_age < m_config.duration) {
        m_spawnTimer += dt * m_config.spawnRate;
        while (m_spawnTimer >= 1.0f) {
            SpawnParticle();
            m_spawnTimer -= 1.0f;
        }
    }

    // Particle simulation
    m_aliveCount = 0;
    for (auto& p : m_particles) {
        if (!p.alive) continue;

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            continue;
        }

        m_aliveCount++;
        float t = p.age / p.lifetime;

        // Size over life
        p.size = p.startSize + t * (p.endSize - p.startSize);

        // Color over life
        p.currentColor.x = p.startColor.x + t * (p.endColor.x - p.startColor.x);
        p.currentColor.y = p.startColor.y + t * (p.endColor.y - p.startColor.y);
        p.currentColor.z = p.startColor.z + t * (p.endColor.z - p.startColor.z);
        p.currentColor.w = p.startColor.w + t * (p.endColor.w - p.startColor.w);

        // Physics: gravity & drag
        p.velocity.x += (p.acceleration.x + m_config.gravity.x) * dt;
        p.velocity.y += (p.acceleration.y + m_config.gravity.y) * dt;
        p.velocity.z += (p.acceleration.z + m_config.gravity.z) * dt;

        float dragFactor = (std::max)(0.0f, 1.0f - m_config.drag * dt);
        p.velocity.x *= dragFactor;
        p.velocity.y *= dragFactor;
        p.velocity.z *= dragFactor;

        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;

        p.rotation += p.rotationSpeed * dt;
    }
}

void ParticleEmitter::GenerateVertices(const XMFLOAT3& cameraPos, const XMFLOAT3& cameraUp,
                                       std::vector<ParticleVertex>& outVertices,
                                       std::vector<uint32_t>& outIndices) const {
    XMVECTOR camPosVec = XMLoadFloat3(&cameraPos);
    XMVECTOR camUpVec  = XMLoadFloat3(&cameraUp);

    for (const auto& p : m_particles) {
        if (!p.alive || p.currentColor.w <= 0.001f || p.size <= 0.001f) continue;

        XMVECTOR pPosVec = XMLoadFloat3(&p.position);
        XMVECTOR forward = XMVector3Normalize(camPosVec - pPosVec);
        if (XMVectorGetX(XMVector3LengthSq(forward)) < 0.0001f) {
            forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }

        XMVECTOR right = XMVector3Normalize(XMVector3Cross(camUpVec, forward));
        XMVECTOR up    = XMVector3Cross(forward, right);

        // Rotation in billboard plane
        float cosR = std::cos(p.rotation);
        float sinR = std::sin(p.rotation);
        XMVECTOR rotRight = right * cosR - up * sinR;
        XMVECTOR rotUp    = right * sinR + up * cosR;

        float h = p.size * 0.5f;
        XMVECTOR extRight = rotRight * h;
        XMVECTOR extUp    = rotUp * h;

        XMVECTOR p0 = pPosVec - extRight - extUp;
        XMVECTOR p1 = pPosVec - extRight + extUp;
        XMVECTOR p2 = pPosVec + extRight + extUp;
        XMVECTOR p3 = pPosVec + extRight - extUp;

        uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

        ParticleVertex v0, v1, v2, v3;
        XMStoreFloat3(&v0.position, p0);
        v0.uv = XMFLOAT2(0.0f, 1.0f);
        v0.color = p.currentColor;

        XMStoreFloat3(&v1.position, p1);
        v1.uv = XMFLOAT2(0.0f, 0.0f);
        v1.color = p.currentColor;

        XMStoreFloat3(&v2.position, p2);
        v2.uv = XMFLOAT2(1.0f, 0.0f);
        v2.color = p.currentColor;

        XMStoreFloat3(&v3.position, p3);
        v3.uv = XMFLOAT2(1.0f, 1.0f);
        v3.color = p.currentColor;

        outVertices.push_back(v0);
        outVertices.push_back(v1);
        outVertices.push_back(v2);
        outVertices.push_back(v3);

        outIndices.push_back(baseIdx + 0);
        outIndices.push_back(baseIdx + 1);
        outIndices.push_back(baseIdx + 2);
        outIndices.push_back(baseIdx + 0);
        outIndices.push_back(baseIdx + 2);
        outIndices.push_back(baseIdx + 3);
    }
}

} // namespace EngineVFX
