#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <algorithm>
#include "TessellationSettings.h"

#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "../../Platform/DirectXMath/DirectXMath.h"
#endif

using namespace DirectX;

class Texture;

namespace EngineRender {

enum class GraphicsAPI {
    DirectX12,
    Metal,
    Vulkan
};

#if defined(_WIN32)
constexpr GraphicsAPI DefaultGraphicsAPI = GraphicsAPI::DirectX12;
#elif defined(__APPLE__)
constexpr GraphicsAPI DefaultGraphicsAPI = GraphicsAPI::Metal;
#else
constexpr GraphicsAPI DefaultGraphicsAPI = GraphicsAPI::Vulkan;
#endif

enum class UpscaleTechnique {
    None = 0,
    DLSS,
    FSR,
    MetalFX,
    XeSS
};

enum class MetalFXMode {
    Disabled = 0,
    Spatial = 1,
    Temporal = 2
};

enum class MetalFXQuality {
    UltraQuality = 0,
    Quality = 1,
    Balanced = 2,
    Performance = 3
};

enum class UpscaleQuality {
    Native = 0,
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

enum class ReflexMode {
    Off = 0,
    LowLatency = 1,
    LowLatencyWithBoost = 2
};

enum class DLSSRRVersion {
    Off = 0,
    Version1_0 = 1,
    Version2_0 = 2
};

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT4 color;
};

struct Meshlet {
    uint32_t VertexOffset;
    uint32_t VertexCount;
    uint32_t PrimitiveOffset;
    uint32_t PrimitiveCount;
    float Center[3];
    float Radius;
    float LODError;
    float MaxParentError;
    uint32_t ParentID;
    XMFLOAT3 ConeNormal;
    float ConeCutoff;
};

constexpr uint32_t MAX_LOD_COUNT = 3;

struct LODLevel {
    uint32_t indexCount = 0;
    uint32_t startIndexLocation = 0;
    int32_t  baseVertexLocation = 0;
    uint32_t meshletStart = 0;
    uint32_t meshletCount = 0;
    float maxDistance = 35.0f; // Max view distance before switching to next LOD
};

enum class MaterialPreset {
    Custom = 0,
    Water,
    Mirror,
    Gold,
    Copper,
    Silver,
    Glass,
    Plastic,
    Wood,
    Concrete,
    Rubber,
    CarPaint
};

struct PBRMaterial {
    TessellationSettings tessellation;
    XMFLOAT3 albedo = { 0.8f, 0.8f, 0.8f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float reflectance = 0.5f;
    float emission = 0.0f;
    float ior = 1.45f;
    XMFLOAT2 uvTiling = { 1.0f, 1.0f };
    MaterialPreset preset = MaterialPreset::Custom;

    std::string albedoPath;
    std::string normalPath;
    std::string roughnessPath;
    std::string metallicPath;
    std::string rmaPath;
    std::string aoPath;
    std::string emissivePath;

    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> rmaMap;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> emissiveMap;

    void ApplyPreset(MaterialPreset p) {
        preset = p;
        switch (p) {
            case MaterialPreset::Water:
                albedo = { 0.05f, 0.25f, 0.45f };
                roughness = 0.02f; metallic = 0.0f; reflectance = 0.8f; ior = 1.333f; emission = 0.0f;
                break;
            case MaterialPreset::Mirror:
                albedo = { 0.98f, 0.98f, 0.98f };
                roughness = 0.01f; metallic = 1.0f; reflectance = 1.0f; ior = 2.5f; emission = 0.0f;
                break;
            case MaterialPreset::Gold:
                albedo = { 1.0f, 0.766f, 0.336f };
                roughness = 0.15f; metallic = 1.0f; reflectance = 1.0f; ior = 0.47f; emission = 0.0f;
                break;
            case MaterialPreset::Copper:
                albedo = { 0.955f, 0.637f, 0.538f };
                roughness = 0.2f; metallic = 1.0f; reflectance = 1.0f; ior = 1.1f; emission = 0.0f;
                break;
            case MaterialPreset::Silver:
                albedo = { 0.972f, 0.960f, 0.915f };
                roughness = 0.1f; metallic = 1.0f; reflectance = 1.0f; ior = 0.18f; emission = 0.0f;
                break;
            case MaterialPreset::Glass:
                albedo = { 0.9f, 0.95f, 1.0f };
                roughness = 0.04f; metallic = 0.0f; reflectance = 0.95f; ior = 1.52f; emission = 0.0f;
                break;
            case MaterialPreset::Plastic:
                albedo = { 0.8f, 0.2f, 0.2f };
                roughness = 0.3f; metallic = 0.0f; reflectance = 0.5f; ior = 1.46f; emission = 0.0f;
                break;
            case MaterialPreset::Wood:
                albedo = { 0.55f, 0.35f, 0.2f };
                roughness = 0.7f; metallic = 0.0f; reflectance = 0.3f; ior = 1.4f; emission = 0.0f;
                break;
            case MaterialPreset::Concrete:
                albedo = { 0.6f, 0.6f, 0.62f };
                roughness = 0.85f; metallic = 0.0f; reflectance = 0.2f; ior = 1.5f; emission = 0.0f;
                break;
            case MaterialPreset::Rubber:
                albedo = { 0.15f, 0.15f, 0.15f };
                roughness = 0.9f; metallic = 0.0f; reflectance = 0.2f; ior = 1.51f; emission = 0.0f;
                break;
            case MaterialPreset::CarPaint:
                albedo = { 0.75f, 0.05f, 0.08f };
                roughness = 0.08f; metallic = 0.85f; reflectance = 0.95f; ior = 1.6f; emission = 0.0f;
                break;
            default:
                break;
        }
    }
};

struct PrimitiveGeometryInfo {
    uint32_t indexCount;
    uint32_t startIndex;
    int32_t  baseVertex;
    uint32_t vertexCount;
    bool valid;
};

constexpr uint32_t MAX_POINT_LIGHTS = 16;
constexpr uint32_t MAX_SPOT_LIGHTS = 16;

// Low-level GPU-packed Point Light data (32 bytes, 16-byte aligned)
struct alignas(16) PointLightData {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    float    range = 10.0f;
    XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float    intensity = 1.0f;
};

// Low-level GPU-packed Spot Light data (64 bytes, 16-byte aligned)
struct alignas(16) SpotLightData {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    float    range = 15.0f;
    XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
    float    intensity = 1.0f;
    XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float    innerConeCos = 0.866025f; // cos(30 deg)
    float    outerConeCos = 0.707107f; // cos(45 deg)
    float    falloff = 1.0f;
    float    enabled = 1.0f;
    float    pad = 0.0f;
};

// High-level Point Light for gameplay, scene, level, and editor
struct PointLight {
    std::string name = "Point Light";
    XMFLOAT3 position = { 0.0f, 2.0f, 0.0f };
    XMFLOAT3 color = { 1.0f, 0.85f, 0.6f };
    float    intensity = 3.0f;
    float    range = 12.0f;
    bool     enabled = true;

    PointLightData ToShaderData() const {
        PointLightData d;
        d.position = position;
        d.range = range;
        d.color = color;
        d.intensity = enabled ? intensity : 0.0f;
        return d;
    }
};

// High-level Spot Light for gameplay, scene, level, and editor
struct SpotLight {
    std::string name = "Spot Light";
    XMFLOAT3 position = { 0.0f, 5.0f, 0.0f };
    XMFLOAT3 direction = { 0.0f, -1.0f, 0.0f };
    XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
    float    intensity = 5.0f;
    float    range = 20.0f;
    float    innerAngle = 20.0f; // degrees
    float    outerAngle = 35.0f; // degrees
    bool     enabled = true;

    SpotLightData ToShaderData() const {
        SpotLightData d;
        d.position = position;
        d.range = range;
        XMVECTOR dirVec = XMVector3Normalize(XMLoadFloat3(&direction));
        XMStoreFloat3(&d.direction, dirVec);
        d.intensity = enabled ? intensity : 0.0f;
        d.color = color;
        float innerRad = XMConvertToRadians((std::min)(innerAngle, outerAngle));
        float outerRad = XMConvertToRadians((std::max)(innerAngle, outerAngle));
        d.innerConeCos = cosf(innerRad);
        d.outerConeCos = cosf(outerRad);
        d.falloff = 1.0f;
        d.enabled = enabled ? 1.0f : 0.0f;
        d.pad = 0.0f;
        return d;
    }
};

struct SceneConstantBuffer {
    XMMATRIX viewProj;
    XMMATRIX prevViewProj;
    XMMATRIX cleanViewProj;
    XMMATRIX prevCleanViewProj;
    XMMATRIX model;
    XMMATRIX sunViewProj;
    XMFLOAT3 cameraPos;
    float    pad1;
    XMFLOAT3 lightDir;
    float    pad2;
    XMFLOAT3 lightColor;
    float    pad3;
    XMFLOAT2 screenRes;
    float    tanHalfFov;
    float    screenHeight;
    uint32_t startIndexLocation;
    int32_t  baseVertexLocation;
    uint32_t triangleCount;
    uint32_t indexFormat;
    uint32_t meshletCount;
    uint32_t meshletStart;
    uint32_t albedoIndex;
    uint32_t normalIndex;
    uint32_t rmaIndex;
    uint32_t aoIndex;
    uint32_t emissiveIndex;
    uint32_t padIndices;
    XMFLOAT3 matAlbedo;
    float    matRoughness;
    float    matMetallic;
    float    matReflectance;
    float    matEmission;
    float    matIOR;
    XMFLOAT2 uvTiling;
    float    rainWetness;
    float    rainPuddles;
    float    cloudDensity;
    float    cloudTime;
    uint32_t numPointLights;
    uint32_t numSpotLights;
    float    padLights1;
    float    padLights2;
    PointLightData pointLights[MAX_POINT_LIGHTS];
    SpotLightData  spotLights[MAX_SPOT_LIGHTS];
    XMMATRIX previousModel;
};

struct RaytracingConstants {
    XMFLOAT4 cameraPos;
    XMFLOAT4 cameraForward;
    XMFLOAT4 cameraRight;
    XMFLOAT4 cameraUp;
    XMFLOAT4 lightDir;
    XMFLOAT4 lightColor;
    int shadowsEnabled;
    int reflectionsEnabled;
    int pathTracingEnabled;
    int maxBounces;
    int frameIndex;
    int samplesPerPixel;
    float cloudDensity;
    float cloudTime;
    float rainWetness;
    float rainPuddles;
    float pad1;
    float pad2;
    uint32_t numPointLights = 0;
    uint32_t numSpotLights = 0;
    XMFLOAT2 lightPadding = {};
    PointLightData pointLights[MAX_POINT_LIGHTS] = {};
    SpotLightData spotLights[MAX_SPOT_LIGHTS] = {};
    XMFLOAT4X4 giPreviousViewProjection = {};
    XMFLOAT4 giPreviousCamera = {};
};

} // namespace EngineRender
