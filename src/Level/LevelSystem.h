#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <DirectXMath.h>
#else
#include "../Networking/NetworkTypes.h"
#endif

#include "Renderer/Common/RenderTypes.h"
#include "Physics/VehicleSettings.h"
#include "Physics/ColliderData.h"
#include "Physics/RagdollSettings.h"

class Renderer;
class Camera;

namespace EngineLevel {

struct LevelEntity {
    std::string name = "Entity";
    std::string filepath = "";
    int primitiveType = -1; // -1 = Dynamic Model (.fbx/.obj), 0=Cube, 1=Sphere, 2=Plane, 3=Floor

    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scale    = { 1.0f, 1.0f, 1.0f };

    bool visible = true;
    bool hasCollision = true;

    // PBR Material
    int materialPreset = 0;
    DirectX::XMFLOAT3 albedo = { 0.8f, 0.8f, 0.8f };
    float roughness = 0.5f;
    float metallic = 0.0f;
    float reflectance = 0.04f;
    float emission = 0.0f;
    float ior = 1.5f;
    DirectX::XMFLOAT2 uvTiling = { 1.0f, 1.0f };

    std::string albedoPath;
    std::string normalPath;
    std::string rmaPath;
    std::string aoPath;
    std::string emissivePath;

    // PhysX Properties
    int physicsBodyType = 0;   // 0=None, 1=Static, 2=Dynamic, 3=Kinematic
    EnginePhysics::ColliderSettings collider;
    EnginePhysics::RagdollSettings ragdollSettings;
    EnginePhysics::VehicleSettings vehicle;
    int physicsShapeType = 0;  // 0=Box, 1=Sphere, 2=Capsule, 3=Plane, 4=ConvexMesh, 5=TriangleMesh
    float physicsMass = 10.0f;
    float physicsLinearDamping=.05f,physicsAngularDamping=.05f;
    float physicsFriction = 0.5f;
    float physicsRestitution = 0.3f;
    bool physicsEnableGravity = true;
    bool physicsEnableCCD = true;
};

struct LevelData {
    std::string format = "Idontknow.Level";
    int version = 1;
    std::string levelName = "main";
    std::string author = "Developer";
    std::string description = "Default Level Map";

    // Player Spawn
    DirectX::XMFLOAT3 spawnPosition = { 0.0f, 1.5f, 0.0f };
    DirectX::XMFLOAT3 spawnRotation = { 0.0f, 0.0f, 0.0f };
    float spawnYaw = 0.0f;

    // Environment & Lighting
    DirectX::XMFLOAT3 skyColor = { 0.2f, 0.4f, 0.8f };
    DirectX::XMFLOAT3 lightDir = { 0.577f, -0.577f, 0.577f };
    DirectX::XMFLOAT3 lightColor = { 1.0f, 0.98f, 0.95f };
    DirectX::XMFLOAT3 ambientColor = { 0.1f, 0.1f, 0.1f };
    DirectX::XMFLOAT3 fogColor = { 0.7f, 0.8f, 0.9f };
    float fogDensity = 0.001f;

    // Weather
    bool rainEnabled = false;
    float rainIntensity = 0.5f;

    // Local Light Sources
    std::vector<EngineRender::PointLight> pointLights;
    std::vector<EngineRender::SpotLight> spotLights;

    // Entities in Level
    std::vector<LevelEntity> entities;

    std::string Serialize() const;
    bool Deserialize(const std::string& text);
};

#pragma pack(push, 1)
struct WorldPakHeader {
    char magic[4];                // 'W', 'P', 'A', 'K' (0x4B415057)
    uint32_t version;             // 1
    char worldName[64];           // level name
    char author[32];              // author name
    uint32_t entityCount;         // total entities
    float spawnX, spawnY, spawnZ; // spawn position
    float spawnYaw;               // spawn yaw
    uint32_t levelDataSize;       // byte size of serialized level data
    uint32_t flags;               // 0 = raw, 1 = deflate
    uint32_t checksum;            // checksum
};
#pragma pack(pop)

class LevelSystem {
public:
    static LevelSystem& Get();

    void Initialize();
    bool LoadLevel(const std::string& levelNameOrPath, Renderer* renderer = nullptr, Camera* camera = nullptr);
    bool SaveLevel(const std::string& levelNameOrPath, Renderer* renderer = nullptr, Camera* camera = nullptr);
    void NewLevel(Renderer* renderer = nullptr, Camera* camera = nullptr);

    // Memory / PAK archive loading
    bool LoadLevelFromMemory(const uint8_t* data, size_t size, Renderer* renderer = nullptr, Camera* camera = nullptr);

    // Project root & ServerMaps directory
    static std::filesystem::path GetProjectRootDir();
    static std::filesystem::path GetServerMapsDirectory();

    // Scene <-> Level synchronization
    void SyncSceneToLevelData(Renderer* renderer, Camera* camera);

    // Dedicated Server world.pak export & load
    bool ExportWorldPak(const std::string& pakFilename = "world.pak",
                        const std::vector<std::string>& destinationFolders = {},
                        Renderer* renderer = nullptr,
                        Camera* camera = nullptr);
    bool ExportLevelFileToWorldPak(const std::string& levelFilePath,
                                  const std::string& pakFilename = "world.pak");
    bool LoadWorldPak(const std::string& pakFilePath,
                      Renderer* renderer = nullptr,
                      Camera* camera = nullptr);

    // Editor UI
    void DrawUI(Renderer* renderer, Camera* camera);

    const LevelData& GetCurrentLevel() const { return m_currentLevel; }
    LevelData& GetCurrentLevel() { return m_currentLevel; }
    const std::string& GetCurrentLevelName() const { return m_currentLevel.levelName; }
    const std::string& GetCurrentLevelPath() const { return m_currentLevelPath; }
    bool IsLoadedFromPak() const { return m_isLoadedFromPak; }

    std::string GetLevelsDirectory();
    std::vector<std::string> GetAvailableLevels();

    // Safe In-Game Runtime Map Loading
    void RequestLoadLevel(const std::string& levelNameOrPath);
    bool HasPendingLoad() const { return m_hasPendingLoad; }
    bool ProcessPendingLoad(Renderer* renderer, Camera* camera);
    void CycleToNextLevel(Renderer* renderer = nullptr, Camera* camera = nullptr);

    void SetOpen(bool open) { m_isOpen = open; }
    bool IsOpen() const { return m_isOpen; }
    void ToggleOpen() { m_isOpen = !m_isOpen; }

private:
    LevelSystem() = default;
    ~LevelSystem() = default;

    std::string ResolveLevelPath(const std::string& levelNameOrPath, bool& outInPak);
    void ApplyLevelToScene(Renderer* renderer, Camera* camera);

    LevelData m_currentLevel;
    std::string m_currentLevelPath = "src/Content/Levels/main.level";
    bool m_isLoadedFromPak = false;
    bool m_isOpen = false;

    // Editor state
    char m_levelNameBuffer[128] = "main";
    char m_authorBuffer[128] = "Developer";
    char m_serverPakFilenameBuffer[128] = "world.pak";
    bool m_exportToServerBuild = true;
    bool m_exportToServerBuildLinux = true;
    bool m_exportToRoot = true;
    bool m_exportToContents = true;
    std::string m_statusMessage;
    float m_statusTimer = 0.0f;
    bool m_statusIsError = false;

    // Deferred runtime loading
    bool m_hasPendingLoad = false;
    std::string m_pendingLevelName;
};

} // namespace EngineLevel
