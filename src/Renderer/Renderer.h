#pragma once
#include <array>
#include <unordered_map>
#include "Scene/GameplaySettings.h"
#if defined(_WIN32)
#include <windows.h>
#include <wrl.h>
#include <d3d12.h>
#include "PipelineStateCache.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_matrix_helpers.h>
using namespace Microsoft::WRL;
#else
#include "Platform/PlatformTypes.h"
#include "Platform/DirectXMath/DirectXMath.h"
#endif

#include <string>
#include <memory>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include "Physics/ColliderData.h"
#include "Physics/RagdollSettings.h"
#include "Physics/VehicleSettings.h"

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))
#include "Core/EngineLog.h"
inline bool EngineRenderTraceEnabled() { return EngineLog::Verbose(); }
#define LOG(s) do { if (EngineLog::ShouldLog(#s)) { std::ostringstream _engineLogStream; _engineLogStream << s; EngineLog::Write(_engineLogStream.str()); } } while(0)
using namespace DirectX;

class Camera;
class TextureStreamer;
class Texture;
#if defined(_WIN32)
#include "Rain/RainSystem.h"
#include "VFX/ParticleSystem.h"
#include "Terrain/TerrainSystem.h"
#include "PostProcess/PostProcessSystem.h"
#endif

#if !defined(_WIN32)
namespace EngineRender {
    class MetalRendererBackend;
    class PS4RendererBackend;
}
#endif

namespace EngineRender {
    class XeSSSystem;
    class FidelityFXSystem;
    class FidelityFXFrameGeneration;
}

namespace EngineStorage {
    class DirectStorageSystem;
}

enum class UpscaleTechnique {
    None = 0,
    DLSS,
    FSR,
    XeSS
};

enum class ReflexMode {
    Off = 0,
    LowLatency = 1,
    LowLatencyWithBoost = 2
};

enum class UpscaleQuality {
    Native = 0,
    Quality,
    Balanced,
    Performance,
    UltraPerformance
};

#include "Renderer/Common/RenderTypes.h"

class Renderer {
public:
    using Vertex = EngineRender::Vertex;
    using Meshlet = EngineRender::Meshlet;

    using MaterialPreset = EngineRender::MaterialPreset;
    using PBRMaterial = EngineRender::PBRMaterial;
    using PointLight = EngineRender::PointLight;
    using SpotLight = EngineRender::SpotLight;
    using PointLightData = EngineRender::PointLightData;
    using SpotLightData = EngineRender::SpotLightData;
    using SceneConstantBuffer = EngineRender::SceneConstantBuffer;

    struct RenderModel {
        std::string name;
        std::string geometryName;
        std::string prefabSource;
        std::string prefabNode;
        EngineScene::GameplaySettings gameplay;
        UINT indexCount;
        UINT startIndexLocation;
        INT baseVertexLocation;
        UINT vertexCount;
        int albedoIndex = -1;
        int normalIndex = -1;
        int rmaIndex = -1;
        int aoIndex = -1;
        int emissiveIndex = -1;
        std::shared_ptr<Texture> albedoMap;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> rmaMap;
        std::shared_ptr<Texture> texture;

        PBRMaterial material;

        XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
        XMFLOAT4X4 worldMatrix;
        XMFLOAT4X4 previousWorldMatrix;

        bool visible = true;
        bool hasCollision = true;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> blas;
        Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshletBuffer;
#else
        void* blas = nullptr;
        void* blasScratch = nullptr;
        void* meshletBuffer = nullptr;
#endif
        UINT meshletStart = 0;
        UINT meshletCount = 0;
        // Melting / Liquefaction Physics State (Floor Liquefaction)
        bool isMelting = false;
        float meltProgress = 0.0f; // 0.0 -> 1.0
        float meltSpeed = 0.85f;
        XMFLOAT3 initialPosition = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 initialScale = { 1.0f, 1.0f, 1.0f };
        float initialRoughness = 0.5f;

        void StartMelting(float speed = 0.85f) {
            if (isMelting && meltProgress >= 0.99f) return;
            if (!isMelting) {
                initialPosition = position;
                initialScale = scale;
                initialRoughness = material.roughness;
                meltProgress = 0.0f;
            }
            meltSpeed = speed;
            isMelting = true;
        }

        void ResetMelting() {
            if (isMelting || meltProgress > 0.0f) {
                position = initialPosition;
                scale = initialScale;
                material.roughness = initialRoughness;
                isMelting = false;
                meltProgress = 0.0f;
                XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
                XMMATRIX R = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
                XMMATRIX T = XMMatrixTranslation(position.x, position.y, position.z);
                XMStoreFloat4x4(&worldMatrix, S * R * T);
            }
        }

        // PhysX Physics Properties
        uint32_t modelId = 0;
        EnginePhysics::ColliderSettings collider;
        EnginePhysics::RagdollSettings ragdollSettings;
        EnginePhysics::VehicleSettings vehicle;
        int physicsBodyType = 0; // 0=None, 1=Static, 2=Dynamic, 3=Kinematic
        int physicsShapeType = 0; // 0=Box, 1=Sphere, 2=Capsule, 3=Plane, 4=ConvexMesh, 5=TriangleMesh
        float physicsMass = 10.0f;
        float physicsFriction = 0.5f;
        float physicsRestitution = 0.3f;
        float physicsLinearDamping = 0.05f;
        float physicsAngularDamping = 0.05f;
        bool physicsEnableGravity = true;
        bool physicsEnableCCD = true;

        // LOD (Level of Detail) State
        EngineRender::LODLevel lods[EngineRender::MAX_LOD_COUNT];
        uint32_t numLODs = 1;
        uint32_t activeLOD = 0;
        float lodBias = 0.0f;

        static uint32_t NextId() {
            static uint32_t s_id = 1;
            return s_id++;
        }

        RenderModel(const std::string& n, UINT ic, UINT sil, INT bvl, UINT vc = 0)
            : name(n), indexCount(ic), startIndexLocation(sil), baseVertexLocation(bvl), vertexCount(vc), modelId(NextId()) {
            XMStoreFloat4x4(&worldMatrix, XMMatrixIdentity());
            XMStoreFloat4x4(&previousWorldMatrix, XMMatrixIdentity());
            lods[0].indexCount = ic;
            lods[0].startIndexLocation = sil;
            lods[0].baseVertexLocation = bvl;
            lods[0].meshletStart = 0;
            lods[0].meshletCount = 0;
            lods[0].maxDistance = 35.0f;
            numLODs = 1;
            activeLOD = 0;
        }
    };

    struct RenderDynamicModel {
        std::string name;
        std::string geometryName;
        std::string prefabSource;
        std::string prefabNode;
        EngineScene::GameplaySettings gameplay;
        std::string filepath;
        EnginePhysics::CollisionMeshData collisionMesh;
#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};
#else
        void* vertexBuffer = nullptr;
        void* indexBuffer = nullptr;
#endif
        UINT indexCount = 0;
        UINT startIndexLocation = 0;
        INT baseVertexLocation = 0;
        int albedoIndex = -1;
        int normalIndex = -1;
        int rmaIndex = -1;
        int aoIndex = -1;
        int emissiveIndex = -1;
        std::shared_ptr<Texture> albedoMap;
        std::shared_ptr<Texture> normalMap;
        std::shared_ptr<Texture> rmaMap;
        std::shared_ptr<Texture> texture;

        PBRMaterial material;

        XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
        XMFLOAT4X4 worldMatrix;
        XMFLOAT4X4 previousWorldMatrix;

        bool visible = true;
        bool hasCollision = true;

        // Melting / Liquefaction Physics State
        bool isMelting = false;
        float meltProgress = 0.0f;
        float meltSpeed = 0.85f;
        XMFLOAT3 initialPosition = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 initialScale = { 1.0f, 1.0f, 1.0f };
        float initialRoughness = 0.5f;

        void StartMelting(float speed = 0.85f) {
            if (isMelting && meltProgress >= 0.99f) return;
            if (!isMelting) {
                initialPosition = position;
                initialScale = scale;
                initialRoughness = material.roughness;
                meltProgress = 0.0f;
            }
            meltSpeed = speed;
            isMelting = true;
        }

        void ResetMelting() {
            if (isMelting || meltProgress > 0.0f) {
                position = initialPosition;
                scale = initialScale;
                material.roughness = initialRoughness;
                isMelting = false;
                meltProgress = 0.0f;
                XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
                XMMATRIX R = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
                XMMATRIX T = XMMatrixTranslation(position.x, position.y, position.z);
                XMStoreFloat4x4(&worldMatrix, S * R * T);
            }
        }

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> blas;
        Microsoft::WRL::ComPtr<ID3D12Resource> blasScratch;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshletBuffer;
#else
        void* blas = nullptr;
        void* blasScratch = nullptr;
        void* meshletBuffer = nullptr;
#endif
        UINT meshletStart = 0;
        UINT meshletCount = 0;

        // PhysX Physics Properties
        uint32_t modelId = RenderModel::NextId();
        EnginePhysics::ColliderSettings collider;
        EnginePhysics::RagdollSettings ragdollSettings;
        EnginePhysics::VehicleSettings vehicle;
        int physicsBodyType = 0; // 0=None, 1=Static, 2=Dynamic, 3=Kinematic
        int physicsShapeType = 0; // 0=Box, 1=Sphere, 2=Capsule, 3=Plane, 4=ConvexMesh, 5=TriangleMesh
        float physicsMass = 10.0f;
        float physicsFriction = 0.5f;
        float physicsRestitution = 0.3f;
        float physicsLinearDamping = 0.05f;
        float physicsAngularDamping = 0.05f;
        bool physicsEnableGravity = true;
        bool physicsEnableCCD = true;

        // LOD (Level of Detail) State
        EngineRender::LODLevel lods[EngineRender::MAX_LOD_COUNT];
        uint32_t numLODs = 1;
        uint32_t activeLOD = 0;
        float lodBias = 0.0f;

        RenderDynamicModel() {
            XMStoreFloat4x4(&worldMatrix, XMMatrixIdentity());
            XMStoreFloat4x4(&previousWorldMatrix, XMMatrixIdentity());
            lods[0].indexCount = 0;
            lods[0].startIndexLocation = 0;
            lods[0].baseVertexLocation = 0;
            lods[0].meshletStart = 0;
            lods[0].meshletCount = 0;
            lods[0].maxDistance = 35.0f;
            numLODs = 1;
            activeLOD = 0;
        }

        RenderDynamicModel(const std::string& n, const std::string& fp, UINT ic, UINT sil, INT bvl)
            : name(n), filepath(fp), indexCount(ic), startIndexLocation(sil), baseVertexLocation(bvl),
              position(0, 0, 0), rotation(0, 0, 0), scale(1, 1, 1),
              visible(true), hasCollision(true), albedoIndex(-1), normalIndex(-1), rmaIndex(-1) {
            XMStoreFloat4x4(&worldMatrix, XMMatrixIdentity());
            XMStoreFloat4x4(&previousWorldMatrix, XMMatrixIdentity());
            lods[0].indexCount = ic;
            lods[0].startIndexLocation = sil;
            lods[0].baseVertexLocation = bvl;
            lods[0].meshletStart = 0;
            lods[0].meshletCount = 0;
            lods[0].maxDistance = 35.0f;
            numLODs = 1;
            activeLOD = 0;
        }
    };

    Renderer(int width, int height);
    ~Renderer();

    bool Initialize(HWND hwnd, Camera* camera);
    void BeginUI();
    void Render(float dt);
    void Resize(int width, int height);
    void RequestResize(int width, int height) {
        if (width <= 0 || height <= 0) return;
        m_pendingResizeWidth = width;
        m_pendingResizeHeight = height;
        m_pendingResize = true;
    }
    void Destroy();
#if defined(_WIN32)
    void DrawPipelineDiagnostics(bool& visible);
#endif
    bool IsDeviceRemoved() const;

    using RaytracingConstants = EngineRender::RaytracingConstants;
    RaytracingConstants m_rtConstants;
#if defined(_WIN32)
    ComPtr<ID3D12Resource> m_rtConstantBuffer;
    void* m_pRtCbvDataBegin = nullptr;
#endif

    XMFLOAT3 GetSkyColor() const { return m_skyColor; }
    void SetSkyColor(const XMFLOAT3& color) { m_skyColor = color; }

    void SetSunAzimuth(float azimuth) { m_sunAzimuth = azimuth; UpdateSunFromAngles(); }
    void SetSunElevation(float elevation) { m_sunElevation = elevation; UpdateSunFromAngles(); }
    float GetSunAzimuth() const { return m_sunAzimuth; }
    float GetSunElevation() const { return m_sunElevation; }

    bool m_dxrEnabled = true;
    bool m_rtShadowsEnabled = true;
    bool m_rtReflectionsEnabled = true;
    bool m_pathTracingEnabled = false;
    int m_ptMaxBounces = 2;
    int m_ptSamplesPerPixel = 1;

    ReflexMode m_reflexMode = ReflexMode::LowLatencyWithBoost;
    bool m_reflexSupported = false;
    float m_reflexLatencyMs = 0.0f;

    bool IsReflexSupported() const { return m_reflexSupported; }
    ReflexMode GetReflexMode() const { return m_reflexMode; }
    void SetReflexMode(ReflexMode mode);
    float GetReflexLatencyMs() const { return m_reflexLatencyMs; }

    bool IsFSRSupported() const { return m_fsrSupported; }
    bool IsFSRActive() const { return m_fsrActive; }
    bool IsXeSSSupported() const { return m_xessSupported; }
    bool IsXeSSActive() const { return m_xessActive; }
    bool IsXeSSFrameGenEnabled() const { return m_xessFrameGenEnabled; }
    void SetXeSSFrameGenEnabled(bool enabled);
    bool IsXeLLSupported() const { return m_xellSupported; }
    bool IsXeLLEnabled() const { return m_xellEnabled; }
    void SetXeLLEnabled(bool enabled, bool boost = false);
#if defined(_WIN32)
    EngineRender::XeSSSystem* GetXeSSSystem() const { return m_xessSystem.get(); }
    bool IsDirectStorageSupported() const;
    EngineStorage::DirectStorageSystem* GetDirectStorage() const { return m_directStorage.get(); }
#endif

    bool IsPathTracingEnabled() const { return m_pathTracingEnabled; }
    void SetPathTracingEnabled(bool enabled) { m_pathTracingEnabled = enabled; InvalidateRaytracingScene(); }
    int GetPathTracingBounces() const { return m_ptMaxBounces; }
    void SetPathTracingBounces(int bounces) { m_ptMaxBounces = (std::clamp)(bounces, 1, 4); }
    bool m_ssaoEnabled = true;
    float m_ssaoRadius = 0.40f;
    float m_ssaoBias = 0.030f;
    float m_ssaoIntensity = 0.85f;
    float m_cloudDensity = 1.0f;
    float m_rainIntensity = 0.0f;
    int m_anisotropy = 16;
    float m_fov = 60.0f;
    bool m_vsmEnabled = true;
    bool m_ssgiEnabled = true;
    float m_ssgiIntensity = 1.0f;

    std::shared_ptr<Texture> LoadTexture(const std::string& filepath);
    static std::string TextureCacheKey(std::string path){std::replace(path.begin(),path.end(),'\\','/');for(char& c:path)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return path;}
    std::shared_ptr<Texture> ReloadTexture(const std::string& path){m_textureCache.erase(TextureCacheKey(path));return LoadTexture(path);}
    uint64_t GetEditorTextureId(const std::shared_ptr<Texture>& texture);

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    // Returns geometry offsets for a named primitive type (Cube, Sphere, Plane, Triangle).
    // Use this in LoadScene to remap model names to correct runtime offsets.
    struct PrimitiveGeometryInfo {
        UINT indexCount = 0;
        UINT startIndex = 0;
        INT  baseVertex = 0;
        UINT vertexCount = 0;
        bool valid = false;
        uint32_t numLODs = 1;
        EngineRender::LODLevel lods[EngineRender::MAX_LOD_COUNT] = {};
    };

    PrimitiveGeometryInfo MakeGeomInfo(UINT ic, UINT si, INT bv, UINT vc, const EngineRender::LODLevel* lodList = nullptr, uint32_t nLods = 1) const {
        PrimitiveGeometryInfo info;
        info.indexCount = ic;
        info.startIndex = si;
        info.baseVertex = bv;
        info.vertexCount = vc;
        info.valid = (ic > 0);
        info.numLODs = (std::min)(nLods, EngineRender::MAX_LOD_COUNT);
        if (lodList && nLods > 0) {
            for (uint32_t i = 0; i < info.numLODs; ++i) {
                info.lods[i] = lodList[i];
            }
        } else {
            info.lods[0].indexCount = ic;
            info.lods[0].startIndexLocation = si;
            info.lods[0].baseVertexLocation = bv;
            info.lods[0].maxDistance = 35.0f;
            info.numLODs = 1;
        }
        return info;
    }

    PrimitiveGeometryInfo GetPrimitiveGeometry(const std::string& typeName, int primitiveType = -1) const {
        if (primitiveType == 0) return MakeGeomInfo(m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount, m_cubeLOD, m_cubeNumLODs);
        if (primitiveType == 1) return MakeGeomInfo(m_sphereIndexCount, m_sphereStartIndex, m_sphereBaseVertex, m_sphereVertexCount, m_sphereLOD, m_sphereNumLODs);
        if (primitiveType == 2) return MakeGeomInfo(m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount, m_planeLOD, m_planeNumLODs);
        if (primitiveType == 3) return MakeGeomInfo(m_floorIndexCount, m_floorStartIndex, m_floorBaseVertex, m_floorVertexCount);
        if (primitiveType == 4) return MakeGeomInfo(m_triangleIndexCount, m_triangleStartIndex, m_triangleBaseVertex, m_triangleVertexCount);

        if (typeName == "House Walls" || typeName == "House Wall")
            return MakeGeomInfo(m_wallIndexCount, m_wallStartIndex, m_wallBaseVertex, m_wallVertexCount);
        if (typeName == "House Roof")
            return MakeGeomInfo(m_roofIndexCount, m_roofStartIndex, m_roofBaseVertex, m_roofVertexCount);
        if (typeName == "Floor")
            return MakeGeomInfo(m_floorIndexCount, m_floorStartIndex, m_floorBaseVertex, m_floorVertexCount);

        // Strip trailing digits/spaces (e.g. "Cube 0" -> "Cube")
        std::string base = typeName;
        size_t sp = base.find(' ');
        if (sp != std::string::npos) base = base.substr(0, sp);

        if (base == "Cube")
            return MakeGeomInfo(m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount, m_cubeLOD, m_cubeNumLODs);
        if (base == "Sphere")
            return MakeGeomInfo(m_sphereIndexCount, m_sphereStartIndex, m_sphereBaseVertex, m_sphereVertexCount, m_sphereLOD, m_sphereNumLODs);
        if (base == "Plane")
            return MakeGeomInfo(m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount, m_planeLOD, m_planeNumLODs);
        if (base == "Triangle")
            return MakeGeomInfo(m_triangleIndexCount, m_triangleStartIndex, m_triangleBaseVertex, m_triangleVertexCount);
        if (base == "Floor")
            return MakeGeomInfo(m_floorIndexCount, m_floorStartIndex, m_floorBaseVertex, m_floorVertexCount);

        // Case-insensitive substring fallback for custom entity names (e.g. "StarterCube", "PlatformFloor")
        std::string lower = typeName;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)::tolower(c); });
        if (lower.find("floor") != std::string::npos)
            return MakeGeomInfo(m_floorIndexCount, m_floorStartIndex, m_floorBaseVertex, m_floorVertexCount);
        if (lower.find("cube") != std::string::npos || lower.find("monolith") != std::string::npos || lower.find("pillar") != std::string::npos)
            return MakeGeomInfo(m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount, m_cubeLOD, m_cubeNumLODs);
        if (lower.find("sphere") != std::string::npos || lower.find("energy_core") != std::string::npos)
            return MakeGeomInfo(m_sphereIndexCount, m_sphereStartIndex, m_sphereBaseVertex, m_sphereVertexCount, m_sphereLOD, m_sphereNumLODs);
        if (lower.find("plane") != std::string::npos)
            return MakeGeomInfo(m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount, m_planeLOD, m_planeNumLODs);
        if (lower.find("triangle") != std::string::npos)
            return MakeGeomInfo(m_triangleIndexCount, m_triangleStartIndex, m_triangleBaseVertex, m_triangleVertexCount);
        if (lower.find("wall") != std::string::npos)
            return MakeGeomInfo(m_wallIndexCount, m_wallStartIndex, m_wallBaseVertex, m_wallVertexCount);
        if (lower.find("roof") != std::string::npos)
            return MakeGeomInfo(m_roofIndexCount, m_roofStartIndex, m_roofBaseVertex, m_roofVertexCount);

        // Safe fallback to Cube geometry to ensure valid indices and prevent DXR / Mesh shader GPU hangs
        if (m_cubeIndexCount > 0)
            return MakeGeomInfo(m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount, m_cubeLOD, m_cubeNumLODs);

        return { 0, 0, 0, 0, false };
    }

    int GetAnisotropy() const { return m_anisotropy; }
#if defined(_WIN32)
    bool GetCollisionMesh(uint32_t modelId, bool dynamic, EnginePhysics::CollisionMeshData& mesh) const;
#else
    bool GetCollisionMesh(uint32_t, bool, EnginePhysics::CollisionMeshData&) const { return false; }
#endif
    void SetAnisotropy(int aniso);

    float GetFOV() const { return m_fov; }
    void SetFOV(float fov) { m_fov = (std::clamp)(fov, 30.0f, 120.0f); }

    bool IsVSMEnabled() const { return m_vsmEnabled; }
    void SetVSMEnabled(bool enabled) { m_vsmEnabled = enabled; }

    bool IsSSGIEnabled() const { return m_ssgiEnabled; }
    void SetSSGIEnabled(bool enabled) { m_ssgiEnabled = enabled; }
    float GetSSGIIntensity() const { return m_ssgiIntensity; }
    void SetSSGIIntensity(float val) { m_ssgiIntensity = std::isfinite(val)?(std::clamp)(val,0.f,3.f):1.f; }

    bool m_volumetricFogEnabled = true;
    float m_volumetricFogDensity = 0.015f;

    bool IsVolumetricFogEnabled() const { return m_volumetricFogEnabled; }
    void SetVolumetricFogEnabled(bool enabled) { m_volumetricFogEnabled = enabled; }
    float GetVolumetricFogDensity() const { return m_volumetricFogDensity; }
    void SetVolumetricFogDensity(float d) { m_volumetricFogDensity = d; }

    bool IsSSAOEnabled() const { return m_ssaoEnabled; }
    void SetSSAOEnabled(bool enabled) { m_ssaoEnabled = enabled; }
    float GetSSAORadius() const { return m_ssaoRadius; }
    void SetSSAORadius(float r) { m_ssaoRadius = r; }
    float GetSSAOBias() const { return m_ssaoBias; }
    void SetSSAOBias(float b) { m_ssaoBias = b; }
    float GetSSAOIntensity() const { return m_ssaoIntensity; }
    void SetSSAOIntensity(float i) { m_ssaoIntensity = i; }

    float GetCloudDensity() const { return m_cloudDensity; }
    void SetCloudDensity(float d) { m_cloudDensity = d; }
    float GetRainIntensity() const { return m_rainIntensity; }
    void SetRainIntensity(float r) { m_rainIntensity = r; }

#if defined(_WIN32)
    void RebuildRaytracingScene();
    void CreateRayTracingPipeline();
    void CreateRayTracingOutputResource();
    void CreateShaderTable();
    void DoRayTracing();
    void LogD3D12Messages();
#endif
    static std::vector<uint8_t> LoadFromPak(const std::wstring& targetFile);

#if defined(_WIN32)
    ComPtr<ID3D12Resource> CreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps);

    ComPtr<ID3D12Device5> m_dxrDevice; 
    ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;
    ComPtr<ID3D12StateObject> m_dxrStateObject;
    ComPtr<ID3D12RootSignature> m_dxrGlobalRootSignature;
    
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    ComPtr<ID3D12Resource> m_instanceDescs;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_cachedRtInstances;
    
    ComPtr<ID3D12Resource> m_rayTracingOutput;
    UINT m_rayTracingOutputUavIndex;
    UINT m_rayTracingOutputSrvIndex;
    UINT m_rayTracingOutputRtvIndex;

    ComPtr<ID3D12Resource> m_shaderTable;
#endif

    TextureStreamer* GetTextureStreamer() const { return m_textureStreamer.get(); }

    UpscaleTechnique GetUpscaleTechnique() const { return m_upscaleTechnique; }
    void SetUpscaleTechnique(UpscaleTechnique technique) { 
        if (m_upscaleTechnique != technique) {
            m_upscaleTechnique = technique; 
            m_pendingResize = true;
        }
    }
    UpscaleQuality GetUpscaleQuality() const { return m_upscaleQuality; }
    void SetUpscaleQuality(UpscaleQuality quality) { 
        if (m_upscaleQuality != quality) {
            m_upscaleQuality = quality; 
            m_pendingResize = true;
        }
    }
    float GetUpscaleSharpness() const { return m_upscaleSharpness; }
    void SetUpscaleSharpness(float sharpness) { m_upscaleSharpness = sharpness; }

    const XMFLOAT3& GetLightDir() const { return m_constantBufferData.lightDir; }
    void SetLightDir(const XMFLOAT3& dir) { m_constantBufferData.lightDir = dir; }
    const XMFLOAT3& GetLightColor() const { return m_baseLightColor; }
    void SetLightColor(const XMFLOAT3& color) { 
        m_baseLightColor = color; 
        m_constantBufferData.lightColor = color; 
    }
    XMFLOAT3 m_baseLightColor = { 1.0f, 0.98f, 0.95f };

    // Local Light Sources (Point & Spot Lights)
    void AddPointLight(const EngineRender::PointLight& light) { m_pointLights.push_back(light); }
    void AddSpotLight(const EngineRender::SpotLight& light) { m_spotLights.push_back(light); }
    void ClearLocalLights() { m_pointLights.clear(); m_spotLights.clear(); }
    std::vector<EngineRender::PointLight>& GetPointLights() { return m_pointLights; }
    const std::vector<EngineRender::PointLight>& GetPointLights() const { return m_pointLights; }
    std::vector<EngineRender::SpotLight>& GetSpotLights() { return m_spotLights; }
    const std::vector<EngineRender::SpotLight>& GetSpotLights() const { return m_spotLights; }

    std::vector<RenderModel>& GetModels() { return m_models; }
    std::vector<RenderDynamicModel>& GetDynamicModels() { return m_dynamicModels; }
    void UpdateModelMeshletRanges();
    void InvalidateRaytracingScene(bool geometryChanged = true) {
#if defined(_WIN32)
        // Transform/visibility changes are detected from TLAS instances each frame.
        if (geometryChanged) m_rtSceneDirty = true;
#else
        m_rtSceneDirty = true;
#endif
    }

    void SpawnCube(const XMFLOAT3& pos);
    void SpawnPlane(const XMFLOAT3& pos);
    void SpawnSphere(const XMFLOAT3& pos);
    void SpawnTriangle(const XMFLOAT3& pos);
    void SpawnDynamicRenderModel(const char* filepath, const XMFLOAT3& pos);
    uint32_t CreateDynamicClothModel(const std::string& name, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, const PBRMaterial& material);
    bool UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices);
    bool UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices);
    bool RemoveDynamicModel(uint32_t modelId);
    void BuildMeshlets(const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<Renderer::Meshlet>& outMeshlets);
#if defined(_WIN32)
    ComPtr<ID3D12Resource> CreateStructuredBuffer(uint32_t count, uint32_t stride, const void* data);
#endif

    void SetDefaultTexture(std::shared_ptr<Texture> tex) { m_defaultTexture = tex; }
    std::shared_ptr<Texture> GetDefaultTexture() const { return m_defaultTexture; }
#if defined(_WIN32)
    ID3D12Device2* GetDevice() const { return m_device.Get(); }
    const D3D12_HEAP_PROPERTIES& GetUploadHeapProps() const { return m_heapPropsUpload; }
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    EngineRender::PS4RendererBackend* GetPS4Backend() const { return m_ps4Backend.get(); }
#else
    EngineRender::MetalRendererBackend* GetMetalBackend() const { return m_metalBackend.get(); }
#endif
    Camera* GetCamera() const { return m_camera; }
    void SetCamera(Camera* camera) { m_camera = camera; }
    float& GetCloudDensity() { return m_cloudDensity; }

    bool IsVSyncEnabled() const { return m_vsyncEnabled; }
    bool IsTAAEnabled() const {
#if defined(_WIN32)
        return m_taaEnabled;
#else
        return false;
#endif
    }
    double GetLastGpuWaitMs() const { return m_lastGpuWaitMs; }
    double GetLastGpuFrameMs() const { return m_lastGpuFrameMs; }
    const std::array<double,10>& GetGpuPassTimes()const{
#if defined(_WIN32)
        return m_gpuPassTimes;
#else
        static const std::array<double,10> empty{};return empty;
#endif
    }
#if defined(_WIN32)
    void SynchronizeForUpdate();
    void BeginSimulationFrame();
    void MarkLatencyStage(int stage);
    static void SetInteractiveErrors(bool enabled);
#else
    void SynchronizeForUpdate() {}
    void BeginSimulationFrame() {}
#endif
    void SetTAAEnabled(bool enabled) {
#if defined(_WIN32)
        if (m_taaEnabled != enabled) m_taaHistoryValid = false; m_taaEnabled = enabled;
#endif
    }
#if defined(_WIN32)
    bool SaveScreenshotBMP(const std::string& path);
#endif
    void SetVSyncEnabled(bool enabled) { m_vsyncEnabled = enabled; }
    bool IsDLSSSupported() const { return m_dlssSupported; }
    bool IsDLSSActive() const { return m_dlssActive; }
    static constexpr bool IsVirtualTexturingIntegrated() { return false; }
    static constexpr bool IsFrameGenerationIntegrated() {
#if defined(_WIN32)
        return true;
#else
        return false;
#endif
    }
    static constexpr bool IsRayReconstructionIntegrated() { return IsFrameGenerationIntegrated(); }
    bool IsDLSSGSupported() const { return m_dlssGSupported && IsFrameGenerationIntegrated(); }
    bool IsDLSSGActive() const { return m_dlssGActive; }
    bool IsDLSSGRequested() const { return m_dlssGRequested; }
    int GetDLSSGMaxMultiplier() const { return m_dlssGMaxMultiplier; }
    int GetDLSSGStatus() const { return m_dlssGStatus; }
    uint64_t GetDLSSGGeneratedFrames() const { return m_dlssGeneratedFrames; }
    void SetDLSSGActive(bool active) { m_dlssGRequested=active&&IsDLSSGSupported(); if(m_dlssGRequested){m_fsrFrameGenEnabled=false;m_xessFrameGenEnabled=false;} }
    int GetDLSSGMultiplier() const { return m_dlssGMultiplier; }
    void SetDLSSGMultiplier(int mult) { m_dlssGMultiplier = (std::clamp)(mult, 2, (std::max)(2,m_dlssGMaxMultiplier)); }

    enum class DLSSRRVersion {
        Off = 0,
        Version1_0 = 1, // SDK preset D
        Version2_0 = 2  // SDK preset E
    };

    bool IsDLSSRayReconstructionSupported() const { return m_dlssRRSupported && IsRayReconstructionIntegrated(); }
    bool IsDLSSRayReconstructionActive() const { return m_dlssRRActive; }
    DLSSRRVersion GetDLSSRayReconstructionVersion() const { return m_dlssRRVersion; }
    void SetDLSSRayReconstructionVersion(DLSSRRVersion ver) {
        if (!IsDLSSRayReconstructionSupported() || static_cast<int>(ver) < 0 || static_cast<int>(ver) > 2) ver = DLSSRRVersion::Off;
        if (ver != m_dlssRRVersion) { m_dlssNeedsReset = true; m_dlssRRActive = false; }
        m_dlssRRVersion = ver;
    }
    void SetDLSSRayReconstructionActive(bool active) {
        SetDLSSRayReconstructionVersion(active ? DLSSRRVersion::Version2_0 : DLSSRRVersion::Off);
    }

    bool IsFSRFrameGenEnabled() const { return m_fsrFrameGenEnabled; }
    void SetFSRFrameGenEnabled(bool enabled) { m_fsrFrameGenEnabled = enabled&&IsFrameGenerationIntegrated(); if(enabled){m_xessFrameGenEnabled=false;m_dlssGRequested=false;} }
    bool IsFSRFrameGenActive() const { return m_fsrFrameGenActive; }
    int GetFSRFrameGenMultiplier() const { return m_fsrFrameGenMultiplier; }
    void SetFSRFrameGenMultiplier(int) { m_fsrFrameGenMultiplier = 2; }

    int GetRenderWidth() const { return m_renderWidth; }
    int GetRenderHeight() const { return m_renderHeight; }

#if defined(_WIN32)
    void WaitForPreviousFrame();
#else
    void WaitForPreviousFrame() {}
#endif

    // LOD (Level of Detail) Settings & Stats
    float GetLODDistanceMultiplier() const { return m_lodDistanceMultiplier; }
    void SetLODDistanceMultiplier(float mult) { m_lodDistanceMultiplier = (std::max)(mult, 0.1f); }
    float GetLODBias() const { return m_lodBias; }
    void SetLODBias(float bias) { m_lodBias = bias; }
    int GetForcedLOD() const { return m_forcedLOD; }
    void SetForcedLOD(int forced) { m_forcedLOD = forced; }
    bool IsLODCullingEnabled() const { return m_enableLODCulling; }
    void SetLODCullingEnabled(bool enable) { m_enableLODCulling = enable; }
    float GetLODCullDistance() const { return m_cullDistance; }
    void SetLODCullDistance(float dist) { m_cullDistance = dist; }
    bool IsDebugLODColor() const { return m_debugLODColor; }
    void SetDebugLODColor(bool enable) { m_debugLODColor = enable; }

    uint32_t GetLOD0Count() const { return m_lod0Count; }
    uint32_t GetLOD1Count() const { return m_lod1Count; }
    uint32_t GetLOD2Count() const { return m_lod2Count; }
    uint32_t GetLODCulledCount() const { return m_lodCulledCount; }

private:
#if defined(_WIN32)
    D3D12_RESOURCE_STATES m_mboitMomentsState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    void BuildBLASForGeometry(const D3D12_RAYTRACING_GEOMETRY_DESC& geomDesc, ComPtr<ID3D12Resource>& blas, ComPtr<ID3D12Resource>& scratch);
    void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter = false);
    
    HRESULT CreateRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc, ID3D12RootSignature** ppRootSignature);
    bool InitFSR2();
    void ShutdownFSR2();
    bool ApplyFSR2(ID3D12GraphicsCommandList* commandList);
    bool InitXeSS();
    void ShutdownXeSS();
    void ApplyXeSS(ID3D12GraphicsCommandList* commandList);
    bool InitDirectStorage();
    void ShutdownDirectStorage();
    void ApplyTAA(ID3D12GraphicsCommandList* commandList);
    void ApplyVolumetricFog(ID3D12GraphicsCommandList* commandList);
    void ApplyReflections(ID3D12GraphicsCommandList* commandList);
#endif

    void SpawnDecal(XMVECTOR position, XMVECTOR normal, float scale, UINT textureIndex);

    static const UINT FrameCount = 2;

    float m_lastDt = 0.016f;
    double m_lastGpuWaitMs = 0.0;
    double m_lastGpuFrameMs = 0.0;
    bool m_hasGpuFrameTiming = false;

    void UpdateSunFromAngles();
    float m_sunAzimuth;
    float m_sunElevation;

    struct Decal {
        XMMATRIX worldMatrix;
        UINT textureIndex;
        XMFLOAT4 color;
        bool active;
    };
    std::vector<Decal> m_decals;

#if defined(_WIN32)
    ComPtr<ID3D12RootSignature> m_rvtDecalRootSignature;
    ComPtr<ID3D12PipelineState> m_rvtDecalPipelineState;
#endif
    int m_width;
    int m_height;
    int m_renderWidth;
    int m_renderHeight;

#if defined(_WIN32)
    ComPtr<IDXGIFactory4> m_dxgiFactory;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device2> m_device;
    EngineRender::PipelineStateCache m_pipelineCache;
    bool m_meshShadersAvailable=false,m_dxrAvailable=false;
    uint64_t m_meshDispatches=0,m_vertexDraws=0,m_computeDispatches=0;
    bool InitializeVertexPipeline();
    void DrawGeometry(UINT groups,UINT indices);
    void DispatchCompute(const char* pass,ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z);
    void InitializeBackendQueries();
    void ReportBackendStatistics();
    ComPtr<ID3D12QueryHeap> m_meshQuery,m_computeQuery;
    ComPtr<ID3D12Resource> m_meshStats,m_computeStats;
    std::unordered_map<std::string,uint64_t> m_computePasses;
    bool m_buildDepthPyramid=false,m_cullingProbeOutside=false;
    uint64_t m_depthPyramidDispatches=0;
    void BuildDepthPyramid();
    ComPtr<ID3D12PipelineState> m_tessellationPipeline,m_tessellationGBufferPipeline,m_tessellationShadowPipeline;
    bool InitializeTessellation();
    uint64_t m_tessellatedDraws=0;
    ComPtr<ID3D12QueryHeap> m_tessellationQuery;
    ComPtr<ID3D12Resource> m_tessellationStatistics;
    bool DrawTessellated(const PBRMaterial& material,UINT indexCount,bool shadow,bool gbuffer);
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12GraphicsCommandList6> m_commandList;
    ComPtr<ID3D12Resource> m_depthStencilBuffer;
    ComPtr<ID3D12Resource> m_shadowMapTexture;
    D3D12_RESOURCE_STATES m_shadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;

    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;

    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource> m_queryResultBuffer;
    ComPtr<ID3D12Resource> m_queryReadbackBuffer;
    UINT64 m_timestampFrequency = 0;
    std::array<double,10> m_gpuPassTimes{};
    static const UINT MaxQueryCount = 100;
    bool m_vrsSupported = false;
    bool m_conservativeRasterSupported = false;

    ComPtr<ID3D12Resource> m_sceneColorTexture;
    ComPtr<ID3D12Resource> m_compositedColorTexture;
    ComPtr<ID3D12Resource> m_velocityTexture;
    ComPtr<ID3D12Resource> m_normalTexture;
    ComPtr<ID3D12Resource> m_ssgiTexture;
    ComPtr<ID3D12Resource> m_giHistory;
    D3D12_RESOURCE_STATES m_giHistoryState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bool m_giHistoryValid = false;
    uint64_t m_giHistoryLastFrame = 0, m_giInstanceSignature = 0, m_giLightingSignature = 0;
    XMFLOAT4X4 m_giPreviousViewProjection = {};
    XMFLOAT4 m_giPreviousCamera = {};
    ComPtr<ID3D12Resource> m_mboitMomentsTexture;
    ComPtr<ID3D12Resource> m_hizTexture;
    UINT m_hizSrvIndex;
    UINT m_hizUavIndex; 

    ComPtr<ID3D12Resource> m_vsmPhysicalAtlas;
    ComPtr<ID3D12Resource> m_vsmPageTable;
    UINT m_vsmPhysicalAtlasRtvIndex;
    UINT m_vsmPhysicalAtlasSrvIndex;
    UINT m_vsmPhysicalAtlasUavIndex;
    UINT m_vsmPageTableSrvIndex;
    UINT m_vsmPageTableUavIndex;

    ComPtr<ID3D12Resource> m_historyColorTexture;
    UINT m_historyColorSrvIndex;
    UINT m_historyColorUavIndex;
    bool m_taaEnabled = false;
    bool m_taaHistoryValid = false;

    ComPtr<ID3D12Resource> m_volumetricFogVolume;
    ComPtr<ID3D12Resource> m_volumetricFogAccumulation;
    UINT m_volumetricFogSrvIndex;
    UINT m_volumetricFogUavIndex;
    UINT m_volumetricFogAccumulationUavIndex;

    ComPtr<ID3D12Resource> m_reflectionTexture;
    UINT m_reflectionSrvIndex;
    UINT m_reflectionUavIndex;

    ComPtr<ID3D12Resource> m_rvtPhysicalAtlasAlbedo;
    ComPtr<ID3D12Resource> m_rvtPhysicalAtlasNormal;
    ComPtr<ID3D12Resource> m_rvtPhysicalAtlasIrradiance;
    ComPtr<ID3D12Resource> m_rvtPageTable;
    UINT m_rvtAlbedoAtlasSrvIndex;
    UINT m_rvtAlbedoAtlasUavIndex;
    UINT m_rvtNormalAtlasSrvIndex;
    UINT m_rvtNormalAtlasUavIndex;
    UINT m_rvtIrradianceAtlasSrvIndex;
    UINT m_rvtIrradianceAtlasUavIndex;
    UINT m_rvtPageTableSrvIndex;
    UINT m_rvtPageTableUavIndex;

    ComPtr<ID3D12Resource> m_meshletsBuffer;
    UINT m_meshletSrvIndex;
#endif
    uint32_t m_meshletCount;
    std::vector<Meshlet> m_cpuMeshlets;
    std::vector<Vertex> m_staticVertices;
    std::vector<uint32_t> m_staticIndices;

#if defined(_WIN32)
    UINT m_sceneColorRtvIndex;
    UINT m_compositedColorRtvIndex;
    UINT m_velocityRtvIndex;
    UINT m_normalRtvIndex;
    UINT m_ssgiSrvIndex;
    UINT m_ssgiUavIndex;
    UINT m_mboitMomentsRtvIndex;
    UINT m_rvtAlbedoAtlasRtvIndex;
    UINT m_rvtNormalAtlasRtvIndex;
    UINT m_rvtIrradianceAtlasRtvIndex;

    UINT m_sceneColorSrvIndex;
    UINT m_compositedColorSrvIndex;
    UINT m_velocitySrvIndex;
    UINT m_normalSrvIndex;
    UINT m_mboitMomentsSrvIndex;
#endif

    UINT m_frameCounter = 0;

    Camera* m_camera;
    HWND m_hwnd;
    std::unique_ptr<TextureStreamer> m_textureStreamer;
    std::vector<std::shared_ptr<Texture>> m_textures;
    std::unordered_map<std::string,std::weak_ptr<Texture>> m_textureCache;
    std::shared_ptr<Texture> m_defaultTexture;

    XMMATRIX m_viewProj;
    XMMATRIX m_cleanViewProj;
    XMMATRIX m_prevCleanViewProj;

    XMFLOAT3 m_skyColor;

    std::vector<RenderModel> m_models;
    std::vector<RenderDynamicModel> m_dynamicModels;

#if defined(_WIN32)
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    ComPtr<ID3D12Resource> m_constantBuffer;
    ComPtr<ID3D12DescriptorHeap> m_mainDescHeap;
    UINT m_cbvSrvDescriptorSize;
    UINT m_nextSrvIndex;

    D3D12_HEAP_PROPERTIES m_heapPropsDefault;
    D3D12_HEAP_PROPERTIES m_heapPropsUpload;
#endif

    void ProcessLoadedTextures();

#if defined(_WIN32)
    ComPtr<ID3D12RootSignature> m_postProcessRootSignature;
    ComPtr<ID3D12PipelineState> m_postProcessPipelineState;

    ComPtr<ID3D12RootSignature> m_compositeRootSignature;
    ComPtr<ID3D12PipelineState> m_compositePipelineState;

    ComPtr<ID3D12RootSignature> m_cloudGatherRootSignature;
    ComPtr<ID3D12PipelineState> m_cloudGatherPipelineState;
    ComPtr<ID3D12RootSignature> m_cloudResolveRootSignature;
    ComPtr<ID3D12PipelineState> m_cloudResolvePipelineState;
    ComPtr<ID3D12Resource> m_cloudConstantBuffer;
    UINT8* m_pCloudCbvDataBegin = nullptr;

    ComPtr<ID3D12RootSignature> m_hizRootSignature;
    ComPtr<ID3D12PipelineState> m_hizPipelineState;

    ComPtr<ID3D12RootSignature> m_vsmManagementRootSignature;
    ComPtr<ID3D12PipelineState> m_vsmManagementPipelineState;
    ComPtr<ID3D12PipelineState> m_vsmShadowPipelineState;

    ComPtr<ID3D12RootSignature> m_rvtRootSignature;
    ComPtr<ID3D12RootSignature> m_rvtBakeRootSignature;
    ComPtr<ID3D12PipelineState> m_rvtBakePipelineState;

    ComPtr<ID3D12RootSignature> m_astraRadiosityRootSignature;
    ComPtr<ID3D12PipelineState> m_astraRadiosityPipelineState;
    ComPtr<ID3D12RootSignature> m_astraRayGenRootSignature;
    ComPtr<ID3D12StateObject> m_astraRayGenStateObject;

    ComPtr<ID3D12RootSignature> m_ssaoRootSignature;
    ComPtr<ID3D12PipelineState> m_ssaoPipelineState;
    ComPtr<ID3D12RootSignature> m_ssaoBlurRootSignature;
    ComPtr<ID3D12PipelineState> m_ssaoBlurPipelineState;
    ComPtr<ID3D12Resource> m_ssaoTexture;
    ComPtr<ID3D12Resource> m_ssaoBlurTexture;
    UINT m_ssaoRtvIndex;
    UINT m_ssaoBlurRtvIndex;
    UINT m_ssaoSrvIndex;
    UINT m_ssaoBlurSrvIndex;
    UINT m_depthSrvIndex;
    ComPtr<ID3D12Resource> m_ssaoConstantBuffer;
    UINT8* m_pSSAOCbvDataBegin = nullptr;
    ComPtr<ID3D12Resource> m_ssaoBlurConstantBuffer;
    ComPtr<ID3D12DescriptorHeap> m_ssaoSrvHeap;
    UINT8* m_pSSAOBlurCbvDataBegin = nullptr;
    ComPtr<ID3D12DescriptorHeap> m_ssaoBlurSrvHeap;

    ComPtr<ID3D12RootSignature> m_taaRootSignature;
    ComPtr<ID3D12PipelineState> m_taaPipelineState;

    ComPtr<ID3D12RootSignature> m_volumetricFogRootSignature;
    ComPtr<ID3D12PipelineState> m_volumetricFogInjectPipelineState;
    ComPtr<ID3D12PipelineState> m_volumetricFogAccumulatePipelineState;

    ComPtr<ID3D12RootSignature> m_ssrRootSignature;
    ComPtr<ID3D12PipelineState> m_ssrPipelineState;
#endif

    struct SSAOConstants {
        XMMATRIX projection;
        XMMATRIX invProjection;
        XMMATRIX view;
        XMFLOAT4 samples[32];
        XMFLOAT4 noiseScale;
        XMFLOAT2 uvScale;
        float radius;
        float bias;
        float intensity;
        float padding;
    };

    struct SSAOBlurConstants {
        XMFLOAT2 texelSize;
        XMFLOAT2 padding;
    };

    struct CloudConstants {
        XMMATRIX invViewProj;
        XMFLOAT3 cameraPos;
        float time;
        XMFLOAT3 lightDir;
        float cloudDensity;
        float padding[40];
    };
    struct MaterialData {
        UINT albedoIndex;
        UINT normalIndex;
        UINT rmaIndex;
        UINT aoIndex;
        UINT emissiveIndex;
        UINT indexFormat;
        UINT startIndexLocation;
        INT baseVertexLocation;
        XMFLOAT3 albedo;
        float roughness;
        float metallic;
        float reflectance;
        float emission;
        float ior;
        XMFLOAT2 uvTiling;
        XMFLOAT2 pad;
    };
#if defined(_WIN32)
    std::vector<MaterialData> m_rtMaterialData;
    ComPtr<ID3D12Resource> m_rtMaterialBuffer;
    void* m_pRtMaterialDataBegin = nullptr;

    static void AssignMeshletRange(UINT startIdx, UINT indexCount, const std::vector<Meshlet>& meshlets, UINT& outStart, UINT& outCount);

    void CreateBLAS();
    void CreateTLAS();

    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
#endif

    float m_cloudTime = 0.0f;
    bool m_imguiInitialized = false;

    SceneConstantBuffer m_constantBufferData;
    std::vector<EngineRender::PointLight> m_pointLights;
    std::vector<EngineRender::SpotLight>  m_spotLights;
#if defined(_WIN32)
    UINT8* m_pCbvDataBegin;

    ComPtr<ID3D12RootSignature> m_skyboxRootSignature;
    ComPtr<ID3D12PipelineState> m_skyboxPipelineState;
    ComPtr<ID3D12Resource> m_skyboxConstantBuffer;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
#endif
    
    struct SkyboxConstantBuffer {
        XMFLOAT4X4 invViewProj;
        XMFLOAT4 lightColor;
        XMFLOAT4 lightDir;
        XMFLOAT4 skyColor;
    };
    SkyboxConstantBuffer m_skyboxConstantBufferData;
#if defined(_WIN32)
    UINT8* m_pSkyboxCbvDataBegin;

    ComPtr<ID3D12DescriptorHeap> m_imguiSrvHeap;
    std::vector<std::shared_ptr<Texture>> m_editorPreviewTextures;
#endif

    UINT m_planeIndexCount = 0, m_planeStartIndex = 0, m_planeVertexCount = 0; INT m_planeBaseVertex = 0;
    UINT m_cubeIndexCount = 0, m_cubeStartIndex = 0, m_cubeVertexCount = 0; INT m_cubeBaseVertex = 0;
    UINT m_sphereIndexCount = 0, m_sphereStartIndex = 0, m_sphereVertexCount = 0; INT m_sphereBaseVertex = 0;
    UINT m_triangleIndexCount = 0, m_triangleStartIndex = 0, m_triangleVertexCount = 0; INT m_triangleBaseVertex = 0;
    UINT m_floorIndexCount = 0, m_floorStartIndex = 0, m_floorVertexCount = 0; INT m_floorBaseVertex = 0;
    UINT m_wallIndexCount = 0, m_wallStartIndex = 0, m_wallVertexCount = 0; INT m_wallBaseVertex = 0;
    UINT m_roofIndexCount = 0, m_roofStartIndex = 0, m_roofVertexCount = 0; INT m_roofBaseVertex = 0;

    EngineRender::LODLevel m_sphereLOD[EngineRender::MAX_LOD_COUNT] = {};
    uint32_t m_sphereNumLODs = 1;
    EngineRender::LODLevel m_planeLOD[EngineRender::MAX_LOD_COUNT] = {};
    uint32_t m_planeNumLODs = 1;
    EngineRender::LODLevel m_cubeLOD[EngineRender::MAX_LOD_COUNT] = {};
    uint32_t m_cubeNumLODs = 1;

    UpscaleTechnique m_upscaleTechnique = UpscaleTechnique::None;
    UpscaleQuality m_upscaleQuality = UpscaleQuality::Quality;
    float m_upscaleSharpness = 0.5f;
    bool m_vsyncEnabled = true;

    bool m_tearingSupported = false;

    bool m_dlssSupported = false;
    bool m_dlssActive = false;
#if defined(_WIN32)
    sl::FrameToken* m_slFrameToken=nullptr;
#endif
    uint32_t m_latencyFrameId=0;
    bool m_reflexOptionsApplied=false,m_reflexFrameValid=false,m_reflexActive=false;
    bool m_xellFrameValid=false,m_xellActive=false;
    bool m_fsrActive = false;
    bool m_fsrSupported = false;
    bool m_dlssNeedsReset = false;
    bool m_dlssGSupported = false;
    bool m_dlssGActive = false;
    bool m_dlssGRequested=false;
    bool m_fsrFrameGenActive=false;
    int  m_dlssGMultiplier = 2;
    int m_dlssGMaxMultiplier=2;
    int m_dlssGAppliedMultiplier=0;
    int m_dlssGStatus=0;
    bool m_frameGenReset=true;
    int m_frameGenLastRequest=0;
    int m_frameGenWidth=0,m_frameGenHeight=0;
    bool m_dlssRRSupported = false;
    bool m_dlssRRActive = false;
    DLSSRRVersion m_dlssRRVersion = DLSSRRVersion::Off;
    bool m_fsrFrameGenEnabled = false;
    int  m_fsrFrameGenMultiplier = 2;
    bool m_xessSupported = false;
    bool m_xessActive = false;
    bool m_xessFrameGenEnabled = false;
    bool m_xellSupported = false;
    bool m_xellEnabled = false;
    bool m_xessNeedsReset = true;
    bool m_slTagsDirty = true;
    bool m_pendingResize = false;
    int  m_pendingResizeWidth = 0;
    int  m_pendingResizeHeight = 0;
    bool m_firstFrameAfterResize = true;
    bool m_deviceRemoved = false;
    bool m_rtSceneDirty = true;
    int  m_framesAfterResize = 0;

#if defined(_WIN32)
    D3D12_RESOURCE_STATES m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_normalState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_ssgiState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_rayTracingOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_compositedColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_ssaoState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_ssaoBlurState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_depthStencilState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    D3D12_RESOURCE_STATES m_upscaledColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES m_backBufferState = D3D12_RESOURCE_STATE_PRESENT;
    D3D_ROOT_SIGNATURE_VERSION m_highestRootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    D3D12_RESOURCE_STATES m_vsmPageTableState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_vsmPhysicalAtlasState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_rvtPhysicalAtlasAlbedoState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_rvtPhysicalAtlasNormalState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_rvtPhysicalAtlasIrradianceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_reflectionTextureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_hizTextureState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_volumetricFogVolumeState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_volumetricFogAccumulationState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES m_historyColorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* res, D3D12_RESOURCE_STATES& currentState, D3D12_RESOURCE_STATES newState);

    ComPtr<ID3D12Resource> m_upscaledColorTexture;
    UINT m_upscaledColorRtvIndex;
    UINT m_upscaledColorSrvIndex;

    ComPtr<ID3D12Resource> m_whiteTextureResource;
    UINT m_whiteSrvIndex = 25;
    UINT m_nullSrvIndex = 22;

    std::vector<ComPtr<ID3D12Resource>> m_retiredResources;

    ComPtr<ID3D12DescriptorHeap> m_ssgiSrvHeap;

    ComPtr<ID3D12RootSignature> m_ssgiRootSignature;
    ComPtr<ID3D12PipelineState> m_ssgiPipelineState;
    ComPtr<ID3D12Resource> m_ssgiConstantBuffer;
    UINT8* m_pSsgiCbvDataBegin;
    
    struct SSGIConstantBuffer {
        XMMATRIX invViewProj;
        XMMATRIX viewProj;
        XMFLOAT3 cameraPos;
        float rayStepSize;
        float maxRayDistance;
        int raySteps;
        float intensity;
        float padding;
    };
    SSGIConstantBuffer m_ssgiConstantBufferData;

    struct FSRConstants {
        DirectX::XMFLOAT2 InputSize;
        DirectX::XMFLOAT2 OutputSize;
        DirectX::XMFLOAT2 InputTexelSize;
        DirectX::XMFLOAT2 OutputTexelSize;
        float             Sharpness;
        float             Padding0;
        DirectX::XMFLOAT2 Padding1;
    };

    ComPtr<ID3D12RootSignature> m_fsrRootSignature;
    ComPtr<ID3D12PipelineState> m_fsrPipelineState;
    ComPtr<ID3D12Resource>      m_fsrConstantBuffer;
    UINT8*                      m_pFsrCbvDataBegin = nullptr;
    bool                        m_fsr2Initialized = false;

    std::unique_ptr<EngineRender::XeSSSystem> m_xessSystem;
    std::unique_ptr<EngineRender::FidelityFXSystem> m_fidelityFX;
    std::unique_ptr<EngineRender::FidelityFXFrameGeneration> m_fidelityFXFrameGeneration;
    ComPtr<ID3D12Resource> m_frameGenHudless;
    D3D12_RESOURCE_STATES m_frameGenHudlessState=D3D12_RESOURCE_STATE_COPY_DEST;
    int m_frameGenProxyProvider=0;
    bool m_xessFrameGenActive=false;
    uint64_t m_xessGeneratedFrames=0;
    bool m_dlssGConfigured=false;
    uint64_t m_dlssGeneratedFrames=0,m_dlssGFenceValue=0;
    ComPtr<ID3D12Fence> m_dlssGFence;
    ComPtr<ID3D12Resource> m_frameGenUI;
    ComPtr<ID3D12Resource> m_rrDiffuseAlbedo,m_rrSpecularAlbedo,m_rrHitDistance;
    bool m_streamlineInitialized=false;
    void AttachStreamlinePresentation();
    void UpdateTechnologySwapChain();
    void PrepareTechnologyFrame(float dt);
    void TagDLSSGFrame(float dt);
    void FinishTechnologyPresent();
    void WriteTechnologyState(const std::string& path);
    ComPtr<ID3D12PipelineState> m_rrPipelineState;
    D3D12_RESOURCE_STATES m_rrDiffuseState=D3D12_RESOURCE_STATE_RENDER_TARGET,m_rrSpecularState=D3D12_RESOURCE_STATE_RENDER_TARGET,m_rrHitDistanceState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_frameGenUIState=D3D12_RESOURCE_STATE_RENDER_TARGET;
    std::unique_ptr<EngineStorage::DirectStorageSystem> m_directStorage;
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    std::unique_ptr<EngineRender::PS4RendererBackend> m_ps4Backend;
#else
    std::unique_ptr<EngineRender::MetalRendererBackend> m_metalBackend;
#endif

    // LOD (Level of Detail) Member Variables
    float m_lodDistanceMultiplier = 1.0f;
    float m_lodBias = 0.0f;
    int m_forcedLOD = -1; // -1 = Auto distance, 0 = LOD0, 1 = LOD1, 2 = LOD2
    bool m_enableLODCulling = true;
    float m_cullDistance = 250.0f;
    bool m_debugLODColor = false;

    uint32_t m_lod0Count = 0;
    uint32_t m_lod1Count = 0;
    uint32_t m_lod2Count = 0;
    uint32_t m_lodCulledCount = 0;
};
