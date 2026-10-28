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

enum class ClothConstraintType {
    Structural = 0,
    Shear,
    Bending
};

struct ClothParticle {
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 prevPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 predictedPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT2 uv = { 0.0f, 0.0f };
    DirectX::XMFLOAT3 initialPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 initialLocalOffset = { 0.0f, 0.0f, 0.0f };
    float invMass = 1.0f; // 0.0f = pinned/fixed
    bool isPinned = false;
    bool isGrabbed = false;
};

struct ClothConstraint {
    uint32_t p1 = 0;
    uint32_t p2 = 0;
    float restLength = 1.0f;
    float stiffness = 1.0f;
    ClothConstraintType type = ClothConstraintType::Structural;
    bool isBroken = false;
    float maxStretchRatio = 1.8f;
};

struct ClothTriangle {
    uint32_t i0 = 0;
    uint32_t i1 = 0;
    uint32_t i2 = 0;
    DirectX::XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
    float area = 0.0f;
    bool isDeleted = false;
};

enum class ClothColliderType {
    Plane = 0,
    Sphere,
    Box,
    Capsule
};

struct ClothCollider {
    ClothColliderType type = ClothColliderType::Sphere;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 size = { 1.0f, 1.0f, 1.0f }; // half-extents for Box
    DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    float radius = 0.5f;
    float height = 1.0f;
    float friction = 0.4f;
    bool dynamic = false;
};

enum class ClothPresetType {
    CustomGrid = 0,
    Flag,
    Curtain,
    Tablecloth,
    Cape,
    Trampoline,
    FreeSheet
};

enum class ClothMaterialType {
    Custom = 0,
    Silk,
    Cotton,
    Velvet,
    Denim,
    Leather,
    GoldThread,
    PlasticSheet
};

struct ClothConfig {
    int numRows = 24;
    int numCols = 24;
    float width = 2.4f;
    float height = 2.4f;
    float totalMass = 2.5f;
    float stretchStiffness = 0.98f;
    float shearStiffness = 0.85f;
    float bendStiffness = 0.50f;
    float damping = 0.988f;
    float dragCoefficient = 0.08f;
    float liftCoefficient = 0.05f;
    float gravityMultiplier = 1.0f;
    int solverIterations = 10;
    int subSteps = 3;
    float collisionThickness = 0.035f;
    float surfaceFriction = 0.35f;
    bool enableSelfCollision = false;
    float selfCollisionRadius = 0.05f;
    bool enableTearing = false;
    float tearThreshold = 1.85f;
    bool enableWind = true;
    bool showWireframe = false;
};

class ClothObject {
public:
    ClothObject(uint32_t id, const std::string& name);
    ~ClothObject();

    uint32_t GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    uint32_t GetDynamicModelId() const { return m_dynamicModelId; }
    void SetDynamicModelId(uint32_t id) { m_dynamicModelId = id; }

    ClothConfig& GetConfig() { return m_config; }
    const ClothConfig& GetConfig() const { return m_config; }

    ClothPresetType GetPresetType() const { return m_presetType; }
    void SetPresetType(ClothPresetType type) { m_presetType = type; }

    ClothMaterialType GetMaterialType() const { return m_materialType; }
    void ApplyMaterialPreset(ClothMaterialType matType);

    Renderer::PBRMaterial& GetMaterial() { return m_material; }
    const Renderer::PBRMaterial& GetMaterial() const { return m_material; }

    const DirectX::XMFLOAT3& GetPosition() const { return m_worldPosition; }
    void SetPosition(const DirectX::XMFLOAT3& pos);
    const DirectX::XMFLOAT3& GetRotation() const { return m_worldRotation; }
    void SetRotation(const DirectX::XMFLOAT3& rot);

    std::vector<ClothParticle>& GetParticles() { return m_particles; }
    const std::vector<ClothParticle>& GetParticles() const { return m_particles; }

    std::vector<ClothConstraint>& GetConstraints() { return m_constraints; }
    const std::vector<ClothConstraint>& GetConstraints() const { return m_constraints; }

    std::vector<ClothTriangle>& GetTriangles() { return m_triangles; }
    const std::vector<ClothTriangle>& GetTriangles() const { return m_triangles; }

    std::vector<uint32_t>& GetIndices() { return m_indices; }
    const std::vector<uint32_t>& GetIndices() const { return m_indices; }

    std::vector<Renderer::Vertex>& GetRenderVertices() { return m_renderVertices; }

    // Grid Initialization & Topology Generation
    void CreateGrid(const DirectX::XMFLOAT3& centerPos, const DirectX::XMFLOAT3& rotationEuler,
                    float width, float height, int rows, int cols,
                    int pinMode = 0); // 0=None, 1=TopEdge, 2=Corners, 3=LeftEdge, 4=TwoCorners, 5=AllPerimeter

    // Physics Simulation Step (XPBD)
    void StepPhysics(float dt, const DirectX::XMFLOAT3& gravity, const DirectX::XMFLOAT3& windVelocity,
                     const std::vector<ClothCollider>& colliders);

    // Synchronize Geometry to DirectX 12 Dynamic Mesh
    void SyncToRenderer(Renderer* renderer);

    // Normal & Tangent Recalculation
    void RecalculateNormals();

    // Pinning Utilities
    void SetPin(uint32_t particleIndex, bool pinned);
    void TogglePin(uint32_t particleIndex);
    void PinTopEdge();
    void PinLeftEdge();
    void PinCorners();
    void PinFourCorners();
    void PinTwoTopCorners();
    void PinAllPerimeter();
    void UnpinAll();

    // Slicing / Knife & Tearing
    void TearConstraint(size_t constraintIndex);
    bool SliceByRay(const DirectX::XMFLOAT3& rayStart, const DirectX::XMFLOAT3& rayDir, float maxDistance);

    // Raycast Picking against Cloth Mesh Triangles
    bool Raycast(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir,
                 float& outDist, uint32_t& outClosestParticle, DirectX::XMFLOAT3& outHitPoint);

    // Reset Simulation to Initial State
    void Reset();

    // Add Force to a specific particle or whole cloth
    void AddForce(const DirectX::XMFLOAT3& force);
    void AddImpulseAtParticle(uint32_t particleIndex, const DirectX::XMFLOAT3& impulse);
    void AddExplosionForce(const DirectX::XMFLOAT3& origin, float radius, float strength);

    bool IsTopologyDirty() const { return m_topologyDirty; }
    void SetTopologyDirty(bool dirty) { m_topologyDirty = dirty; }

private:
    uint32_t m_id = 0;
    std::string m_name;
    uint32_t m_dynamicModelId = 0;
    ClothConfig m_config;
    ClothPresetType m_presetType = ClothPresetType::CustomGrid;
    ClothMaterialType m_materialType = ClothMaterialType::Cotton;
    Renderer::PBRMaterial m_material;

    DirectX::XMFLOAT3 m_worldPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_worldRotation = { 0.0f, 0.0f, 0.0f };

    std::vector<ClothParticle> m_particles;
    std::vector<ClothConstraint> m_constraints;
    std::vector<ClothTriangle> m_triangles;
    std::vector<uint32_t> m_indices;
    std::vector<Renderer::Vertex> m_renderVertices;

    bool m_topologyDirty = true;

    // XPBD Constraint Solver Sub-methods
    void SolveDistanceConstraints();
    void SolveCollisions(const std::vector<ClothCollider>& colliders);
    void SolveSelfCollisions();
    void ApplyAerodynamics(const DirectX::XMFLOAT3& windVelocity, float dt);
};

enum class ClothInteractionMode {
    Grab = 0,
    PinBrush,
    Knife,
    ImpulsePush
};

class ClothSystem {
public:
    static ClothSystem& Get();

    bool Initialize(Renderer* renderer);
    void Shutdown(Renderer* renderer);

    void Update(float dt, Renderer* renderer, Window* window, Camera* camera);
    void DrawUI(Renderer* renderer, Camera* camera);

    // Preset Spawning Factory
    std::shared_ptr<ClothObject> SpawnPreset(ClothPresetType type, const DirectX::XMFLOAT3& pos, Renderer* renderer);
    std::shared_ptr<ClothObject> SpawnFlag(const DirectX::XMFLOAT3& pos, float width = 2.4f, float height = 1.6f, int rows = 24, int cols = 32, Renderer* renderer = nullptr);
    std::shared_ptr<ClothObject> SpawnCurtain(const DirectX::XMFLOAT3& pos, float width = 3.0f, float height = 2.5f, int rows = 28, int cols = 32, Renderer* renderer = nullptr);
    std::shared_ptr<ClothObject> SpawnTablecloth(const DirectX::XMFLOAT3& pos, float size = 2.6f, int resolution = 28, Renderer* renderer = nullptr);
    std::shared_ptr<ClothObject> SpawnCape(const DirectX::XMFLOAT3& pos, float width = 1.4f, float height = 2.2f, int rows = 25, int cols = 20, Renderer* renderer = nullptr);
    std::shared_ptr<ClothObject> SpawnTrampoline(const DirectX::XMFLOAT3& pos, float size = 3.2f, int resolution = 26, Renderer* renderer = nullptr);
    std::shared_ptr<ClothObject> SpawnFreeSheet(const DirectX::XMFLOAT3& pos, float width = 2.5f, float height = 2.5f, int rows = 24, int cols = 24, Renderer* renderer = nullptr);

    void RemoveCloth(uint32_t clothId, Renderer* renderer);
    void ClearAllCloths(Renderer* renderer);
    void ResetAllCloths();

    std::vector<std::shared_ptr<ClothObject>>& GetCloths() { return m_cloths; }
    std::shared_ptr<ClothObject> GetClothById(uint32_t id);
    std::shared_ptr<ClothObject> GetSelectedCloth();
    void SetSelectedClothId(uint32_t id) { m_selectedClothId = id; }

    // Global Wind Control
    bool IsWindEnabled() const { return m_windEnabled; }
    void SetWindEnabled(bool enabled) { m_windEnabled = enabled; }
    const DirectX::XMFLOAT3& GetWindDirection() const { return m_windDirection; }
    void SetWindDirection(const DirectX::XMFLOAT3& dir) { m_windDirection = dir; }
    float GetWindSpeed() const { return m_windSpeed; }
    void SetWindSpeed(float speed) { m_windSpeed = speed; }
    float GetWindGustStrength() const { return m_windGustStrength; }
    void SetWindGustStrength(float val) { m_windGustStrength = val; }
    DirectX::XMFLOAT3 CalculateWindAt(const DirectX::XMFLOAT3& pos, float time) const;

    // Colliders Collection
    void CollectSceneColliders(Renderer* renderer);
    std::vector<ClothCollider>& GetColliders() { return m_colliders; }
    void AddCustomCollider(const ClothCollider& col) { m_customColliders.push_back(col); }
    void ClearCustomColliders() { m_customColliders.clear(); }

    // Simulation control
    bool IsPaused() const { return m_isPaused; }
    void SetPaused(bool paused) { m_isPaused = paused; }
    void SingleStep() { m_singleStep = true; }

    // Interactive Tools
    ClothInteractionMode GetInteractionMode() const { return m_interactionMode; }
    void SetInteractionMode(ClothInteractionMode mode) { m_interactionMode = mode; }

    float GetLastSimTimeMs() const { return m_lastSimTimeMs; }
    size_t GetTotalParticles() const;
    size_t GetTotalConstraints() const;

private:
    ClothSystem();
    ~ClothSystem();
    ClothSystem(const ClothSystem&) = delete;
    ClothSystem& operator=(const ClothSystem&) = delete;

    std::vector<std::shared_ptr<ClothObject>> m_cloths;
    std::vector<ClothCollider> m_colliders;
    std::vector<ClothCollider> m_customColliders;
    uint32_t m_selectedClothId = 0;
    uint32_t m_nextClothId = 1;

    // Simulation state
    bool m_isPaused = false;
    bool m_singleStep = false;
    float m_fixedTimestep = 1.0f / 60.0f;
    float m_timeAccumulator = 0.0f;
    float m_simulationTime = 0.0f;
    float m_lastSimTimeMs = 0.0f;

    // Wind parameters
    bool m_windEnabled = true;
    DirectX::XMFLOAT3 m_windDirection = { 1.0f, 0.15f, 0.35f };
    float m_windSpeed = 8.0f;
    float m_windGustStrength = 4.5f;
    float m_windGustFrequency = 0.75f;
    float m_windTurbulence = 0.65f;
    float m_windAzimuth = 45.0f;
    float m_windElevation = 5.0f;

    // Interactive mouse / tool interaction
    ClothInteractionMode m_interactionMode = ClothInteractionMode::Grab;
    bool m_isInteracting = false;
    uint32_t m_grabbedClothId = 0;
    uint32_t m_grabbedParticleIdx = 0;
    float m_grabDistance = 4.0f;
    DirectX::XMFLOAT3 m_grabTargetPos = { 0.0f, 0.0f, 0.0f };

    // Slicing trail
    bool m_isCutting = false;
    DirectX::XMFLOAT3 m_knifePrevRayStart = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_knifePrevRayDir = { 0.0f, 0.0f, 0.0f };

    // UI creation presets parameters
    int m_spawnPresetIdx = 1; // 1 = Flag
    float m_spawnPos[3] = { 0.0f, 2.5f, 0.0f };
    float m_spawnWidth = 2.4f;
    float m_spawnHeight = 1.8f;
    int m_spawnRows = 24;
    int m_spawnCols = 30;
    int m_spawnPinMode = 3; // LeftEdge

    void UpdateWindFromAngles();
};

} // namespace EnginePhysics
