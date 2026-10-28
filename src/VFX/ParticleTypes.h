#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <string>

namespace EngineVFX {

using namespace DirectX;

struct ParticleVertex {
    XMFLOAT3 position;
    XMFLOAT2 uv;
    XMFLOAT4 color;
};

enum class EmitterType {
    Point = 0,
    Sphere,
    Box,
    Cone
};

enum class BlendMode {
    Alpha = 0,
    Additive
};

struct Particle {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 acceleration = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT4 endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    XMFLOAT4 currentColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float size = 0.5f;
    float startSize = 0.2f;
    float endSize = 1.0f;
    float rotation = 0.0f;
    float rotationSpeed = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    bool alive = false;
};

struct ParticleConstantBuffer {
    XMMATRIX viewProj;
    XMFLOAT4 cameraPosAndTime;
};

} // namespace EngineVFX
