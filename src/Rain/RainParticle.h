#pragma once
#include <DirectXMath.h>
#include <vector>

namespace EngineRain {

using namespace DirectX;

struct RainDrop {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 velocity = { 0.0f, -20.0f, 0.0f };
    float length = 0.45f;
    float width = 0.015f;
    float alpha = 0.8f;
    float randomSeed = 0.0f;
    bool alive = true;
};

struct RainSplash {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    float radius = 0.05f;
    float maxRadius = 0.28f;
    float life = 0.0f;
    float maxLife = 0.22f;
    float alpha = 0.95f;
    int type = 0; // 0 = Floor impact ripple ring, 1 = Vertical splash droplet bead, 2 = Vertical splash crown burst
    bool active = false;
};

struct RainVertex {
    XMFLOAT3 position;
    XMFLOAT2 uv;
    XMFLOAT4 color;
};

} // namespace EngineRain
