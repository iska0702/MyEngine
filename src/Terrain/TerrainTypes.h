#pragma once

#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <string>

namespace EngineTerrain {

using namespace DirectX;

struct TerrainVertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
    XMFLOAT4 splat; // x = Grass, y = Dirt, z = Rock, w = Snow
};

struct FoliageVertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 uv;
    XMFLOAT4 color;
};

struct FoliageInstance {
    XMFLOAT3 position;
    float scale = 1.0f;
    float rotation = 0.0f;
    float windPhase = 0.0f;
};

struct TerrainConfig {
    int width = 128;
    int length = 128;
    float cellSize = 1.0f;
    float maxHeight = 16.0f;
    float noiseScale = 0.03f;
    int octaves = 4;
    float persistence = 0.45f;
    float lacunarity = 2.0f;
    int seed = 1337;
    float rockSlopeThreshold = 0.55f;
    float snowHeightThreshold = 11.5f;
    bool enabled = true;
};

struct FoliageConfig {
    bool enabled = true;
    size_t instanceCount = 3500;
    float grassHeight = 0.8f;
    float grassWidth = 0.35f;
    float windSpeed = 2.8f;
    float windStrength = 0.3f;
    float maxDistance = 60.0f;
};

struct TerrainConstantBuffer {
    XMMATRIX viewProj;
    XMFLOAT4 lightDir;
    XMFLOAT4 lightColor;
    XMFLOAT4 cameraPosAndTime;
    XMFLOAT4 windParams; // x = speed, y = strength, z = time, w = unused
};

} // namespace EngineTerrain
