#pragma once

#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "Renderer/Renderer.h"

class Window;
class Camera;

namespace EnginePhysics {

struct HairNode {
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 prevPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 predictedPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 restLocalPos = { 0.0f, 0.0f, 0.0f }; // Rest position relative to head frame
    DirectX::XMFLOAT3 restDirection = { 0.0f, -1.0f, 0.0f }; // Direction to next node in head frame
    DirectX::XMFLOAT3 tangent = { 0.0f, -1.0f, 0.0f };
    DirectX::XMFLOAT3 normal = { 0.0f, 0.0f, 1.0f };
    float restLength = 0.08f;
    float invMass = 1.0f; // 0.0f for root node
    bool isGrabbed = false;
};

struct HairStrand {
    std::vector<HairNode> nodes;
    DirectX::XMFLOAT3 scalpLocalPos = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 scalpLocalNormal = { 0.0f, 1.0f, 0.0f };
    float lengthMultiplier = 1.0f;
    float curlPhase = 0.0f;
    float wavinessPhase = 0.0f;
    bool isCut = false;
    int activeNodeCount = 0;
};

enum class HairColliderType {
    HeadEllipsoid = 0,
    NeckCapsule,
    ShoulderCapsule,
    FloorPlane,
    Sphere,
    Box
};

struct HairCollider {
    HairColliderType type = HairColliderType::HeadEllipsoid;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 radii = { 0.38f, 0.44f, 0.40f }; // for Ellipsoid / Box
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    float radius = 0.35f;
    float height = 0.40f;
    float friction = 0.40f;
};

enum class HairHairstyleType {
    LongFlowing = 0,
    Ponytail,
    BobCut,
    TwinTails,
    CurlyWavy,
    FurBall,
    CustomGroom
};

struct HairConfig {
    int numStrands = 96;
    int nodesPerStrand = 10;
    float hairLength = 0.85f;
    float rootWidth = 0.018f;
    float tipWidth = 0.004f;
    float stretchStiffness = 0.99f;
    float bendStiffness = 0.65f;
    float shapeRetention = 0.35f;
    float damping = 0.982f;
    float airDrag = 0.12f;
    float gravityMultiplier = 1.0f;
    int solverIterations = 4;
    int subSteps = 2;

    // Head and body collision geometry
    float headRadiusX = 0.38f;
    float headRadiusY = 0.44f;
    float headRadiusZ = 0.40f;
    float neckRadius = 0.18f;
    float neckLength = 0.35f;
    float shoulderRadius = 0.20f;
    float shoulderWidth = 0.90f;

    // Styling properties
    float wavinessFreq = 4.0f;
    float wavinessAmp = 0.035f;
    float curlRadius = 0.025f;

    // Appearance & PBR Material
    DirectX::XMFLOAT3 rootColor = { 0.14f, 0.08f, 0.04f }; // Dark chestnut brown
    DirectX::XMFLOAT3 tipColor = { 0.72f, 0.48f, 0.22f };  // Caramel highlights
    float roughness = 0.40f;
    float metallic = 0.08f;
    float specularStrength = 0.85f;
    bool enableWind = true;
};

enum class HairInteractionMode {
    Inspect = 0,
    Grab,
    Comb,
    Scissors
};

class HairObject {
public:
    HairObject(uint32_t id, const std::string& name);
    ~HairObject();

    uint32_t GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    uint32_t GetDynamicModelId() const { return m_dynamicModelId; }
    void SetDynamicModelId(uint32_t id) { m_dynamicModelId = id; }

    HairConfig& GetConfig() { return m_config; }
    const HairConfig& GetConfig() const { return m_config; }

    HairHairstyleType GetHairstyleType() const { return m_hairstyleType; }
    void SetHairstyleType(HairHairstyleType type) { m_hairstyleType = type; }

    const DirectX::XMFLOAT3& GetHeadPosition() const { return m_headPosition; }
    void SetHeadPosition(const DirectX::XMFLOAT3& pos) { m_headPosition = pos; }
    const DirectX::XMFLOAT3& GetHeadRotation() const { return m_headRotation; }
    void SetHeadRotation(const DirectX::XMFLOAT3& rot) { m_headRotation = rot; }

    std::vector<HairStrand>& GetStrands() { return m_strands; }
    const std::vector<HairStrand>& GetStrands() const { return m_strands; }

    std::vector<Renderer::Vertex>& GetRenderVertices() { return m_renderVertices; }
    std::vector<uint32_t>& GetIndices() { return m_indices; }

    Renderer::PBRMaterial& GetMaterial() { return m_material; }
    const Renderer::PBRMaterial& GetMaterial() const { return m_material; }

    // Hairstyle Generation & Grooming
    void GenerateHairstyle(HairHairstyleType type);

    // Physics Simulation (XPBD Cosserat Rod & Chain Dynamics)
    void StepPhysics(float dt, const DirectX::XMFLOAT3& gravity, const DirectX::XMFLOAT3& windVec,
                     const std::vector<HairCollider>& externalColliders);

    // Ribbon Tessellation with Catmull-Rom Smoothing
    void TessellateRibbons(const DirectX::XMFLOAT3& cameraPos);

    // Synchronize to DirectX 12 Dynamic Model
    void SyncToRenderer(Renderer* renderer);

    // Interactive Cutting, Raycasting & Brushing
    bool Raycast(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir,
                 float& outDist, uint32_t& outStrandIdx, uint32_t& outNodeIdx, DirectX::XMFLOAT3& outHitPoint);
    void CutStrands(const DirectX::XMFLOAT3& rayStart, const DirectX::XMFLOAT3& rayDir, float maxDist);
    void BrushHair(const DirectX::XMFLOAT3& brushOrigin, const DirectX::XMFLOAT3& brushDir, float radius, float strength);
    void Reset();

    // Color Palette presets
    void ApplyColorPreset(int presetIdx); // 0=Brunette, 1=Blonde, 2=Raven Black, 3=Crimson Red, 4=Platinum, 5=Pink, 6=Cyan

private:
    uint32_t m_id = 0;
    std::string m_name;
    uint32_t m_dynamicModelId = 0;
    HairConfig m_config;
    HairHairstyleType m_hairstyleType = HairHairstyleType::LongFlowing;
    Renderer::PBRMaterial m_material;

    DirectX::XMFLOAT3 m_headPosition = { 0.0f, 2.5f, 0.0f };
    DirectX::XMFLOAT3 m_headRotation = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_prevHeadPosition = { 0.0f, 2.5f, 0.0f };
    DirectX::XMFLOAT3 m_prevHeadRotation = { 0.0f, 0.0f, 0.0f };

    std::vector<HairStrand> m_strands;
    std::vector<Renderer::Vertex> m_renderVertices;
    std::vector<uint32_t> m_indices;
    bool m_topologyDirty = true;

    void SolveStrandConstraints(HairStrand& strand, const DirectX::XMMATRIX& headTransform);
    void SolveCollisions(HairNode& node, const std::vector<HairCollider>& externalColliders);
};

class HairSystem {
public:
    static HairSystem& Get();

    bool Initialize(Renderer* renderer);
    void Shutdown(Renderer* renderer);

    void Update(float dt, Renderer* renderer, Window* window, Camera* camera);
    void DrawUI(Renderer* renderer, Camera* camera);

    // Hairstyle Spawner
    std::shared_ptr<HairObject> SpawnHairstyle(HairHairstyleType type, const DirectX::XMFLOAT3& pos, Renderer* renderer);
    void RemoveHairObject(uint32_t id, Renderer* renderer);
    void ClearAll(Renderer* renderer);
    void ResetAll();

    std::vector<std::shared_ptr<HairObject>>& GetHairObjects() { return m_hairObjects; }
    std::shared_ptr<HairObject> GetHairObjectById(uint32_t id);
    std::shared_ptr<HairObject> GetSelectedHair();
    void SetSelectedHairId(uint32_t id) { m_selectedHairId = id; }

    // Head Animation & Motion Controller
    bool IsHeadMotionEnabled() const { return m_headMotionEnabled; }
    void SetHeadMotionEnabled(bool enabled) { m_headMotionEnabled = enabled; }

    bool IsPaused() const { return m_isPaused; }
    void SetPaused(bool paused) { m_isPaused = paused; }
    void SingleStep() { m_singleStep = true; }

    float GetLastSimTimeMs() const { return m_lastSimTimeMs; }
    size_t GetTotalStrands() const;
    size_t GetTotalNodes() const;

private:
    HairSystem();
    ~HairSystem();
    HairSystem(const HairSystem&) = delete;
    HairSystem& operator=(const HairSystem&) = delete;

    std::vector<std::shared_ptr<HairObject>> m_hairObjects;
    uint32_t m_selectedHairId = 0;
    uint32_t m_nextHairId = 1;

    // Simulation timing
    bool m_isPaused = false;
    bool m_singleStep = false;
    float m_fixedTimestep = 1.0f / 60.0f;
    float m_simulationTime = 0.0f;
    float m_lastSimTimeMs = 0.0f;

    // Head Motion Controller State
    bool m_headMotionEnabled = false;
    int m_headMotionMode = 1; // 0=Static, 1=Turn Left/Right, 2=Head Nodding, 3=Circle Whip Shake, 4=Breathing
    float m_headMotionSpeed = 2.0f;
    float m_headMotionAmplitude = 45.0f; // degrees
    float m_manualHeadPos[3] = { 0.0f, 2.5f, 0.0f };
    float m_manualHeadRot[3] = { 0.0f, 0.0f, 0.0f };

    // Interactive Tools
    HairInteractionMode m_interactionMode = HairInteractionMode::Grab;
    bool m_isInteracting = false;
    uint32_t m_grabbedHairId = 0;
    uint32_t m_grabbedStrandIdx = 0;
    uint32_t m_grabbedNodeIdx = 0;
    float m_grabDistance = 3.5f;

    // UI Spawning Parameters
    int m_spawnPresetIdx = 0; // 0=LongFlowing
    float m_spawnPos[3] = { 0.0f, 2.5f, 0.0f };
    int m_spawnStrandCount = 180;
    float m_spawnLength = 0.85f;
    int m_spawnColorPreset = 0;
};

} // namespace EnginePhysics
