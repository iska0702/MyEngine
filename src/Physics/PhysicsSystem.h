#pragma once

#include "Platform/PlatformTypes.h"
#include <PxPhysicsAPI.h>
#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "Platform/DirectXMath/DirectXMath.h"
#endif
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include "ColliderData.h"
#include "AI/VehicleRoadAI.h"

namespace physx {
    class PxFoundation;
    class PxPhysics;
    class PxDefaultCpuDispatcher;
    class PxScene;
    class PxMaterial;
    class PxRigidActor;
    class PxRigidDynamic;
    class PxRigidStatic;
    class PxPvd;
}

class Renderer;
class Camera;
class Window;

namespace EnginePhysics {

struct VehicleDynamics;

enum class BodyType {
    None = 0,
    Static,
    Dynamic,
    Kinematic
};

enum class ShapeType {
    Box = 0,
    Sphere,
    Capsule,
    Plane,
    ConvexMesh,
    TriangleMesh
};

struct PhysicsBodyDesc {
    BodyType type = BodyType::None;
    ShapeType shape = ShapeType::Box;
    float mass = 10.0f;
    float staticFriction = 0.5f;
    float dynamicFriction = 0.5f;
    float restitution = 0.3f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool enableGravity = true;
    bool enableCCD = true;
    DirectX::XMFLOAT3 initialPosition = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 initialRotation = { 0.0f, 0.0f, 0.0f };
};

struct RaycastHitInfo {
    bool hasHit = false;
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 normal = { 0.0f, 1.0f, 0.0f };
    float distance = 0.0f;
    physx::PxRigidActor* hitActor = nullptr;
    uint32_t ownerModelId = 0;
    void* ownerModel = nullptr;
    bool isDynamicModel = false;
};

// Math conversion utilities between PhysX and DirectXMath
inline DirectX::XMFLOAT3 PxToXM3(const physx::PxVec3& v) {
    return DirectX::XMFLOAT3(v.x, v.y, v.z);
}

inline physx::PxVec3 XM3ToPx(const DirectX::XMFLOAT3& v) {
    return physx::PxVec3(v.x, v.y, v.z);
}

inline DirectX::XMFLOAT3 QuatToEuler(const physx::PxQuat& q) {
    using namespace DirectX;
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, XMMatrixRotationQuaternion(XMVectorSet(q.x,q.y,q.z,q.w)));
    float pitch=std::asin((std::clamp)(-m._32,-1.0f,1.0f));
    float yaw, roll;
    if(std::abs(std::cos(pitch))>.0001f) { yaw=std::atan2(m._31,m._33); roll=std::atan2(m._12,m._22); }
    else { yaw=std::atan2(-m._13,m._11); roll=0; }
    return {XMConvertToDegrees(pitch),XMConvertToDegrees(yaw),XMConvertToDegrees(roll)};
}

inline physx::PxQuat EulerToPxQuat(const DirectX::XMFLOAT3& rot) {
    using namespace DirectX;
    XMVECTOR q = XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rot.x), XMConvertToRadians(rot.y), XMConvertToRadians(rot.z));
    XMFLOAT4 qf;
    XMStoreFloat4(&qf, q);
    return physx::PxQuat(qf.x, qf.y, qf.z, qf.w);
}

inline DirectX::XMMATRIX PxTransformToMatrix(const physx::PxTransform& t, const DirectX::XMFLOAT3& scale) {
    using namespace DirectX;
    XMMATRIX S = XMMatrixScaling(scale.x, scale.y, scale.z);
    XMVECTOR q = XMVectorSet(t.q.x, t.q.y, t.q.z, t.q.w);
    XMMATRIX R = XMMatrixRotationQuaternion(q);
    XMMATRIX T = XMMatrixTranslation(t.p.x, t.p.y, t.p.z);
    return S * R * T;
}

class PhysicsSystem {
public:
    static PhysicsSystem& Get();

    bool Initialize(Renderer* renderer);
    void Shutdown();

    void Update(float dt, Renderer* renderer, Window* window, Camera* camera);

    uint32_t SpawnVehicle(Renderer* renderer,const DirectX::XMFLOAT3& position);
    int VehicleContacts(uint32_t id) const {auto i=m_vehicles.find(id);return i==m_vehicles.end()?0:i->second.contacts;}
    float VehicleRPM(uint32_t id) const;
    int VehicleGear(uint32_t id) const;
    float VehicleSlip(uint32_t id) const;
    void SetVehicleGear(uint32_t id,int gear);
    float VehicleSpeed(uint32_t id) const {auto i=m_vehicles.find(id);return i==m_vehicles.end()?0:i->second.speed;}
    bool IsDriving() const { return m_drivenVehicle!=0; }
    void SetVehicleInput(uint32_t id,float throttle,float steer,float brake,bool handbrake=false);
    void ResetVehicleRoadAI(uint32_t id) { auto i=m_vehicles.find(id); if(i!=m_vehicles.end()) i->second.road={}; }
    void StepVehicles(float dt,Renderer* renderer);
    void SimulateStep(float dt,Renderer* renderer);
    void UpdateVehicleVisuals(Renderer* renderer);
    void UpdateVehicleInput(Renderer* renderer,Window* window,Camera* camera);
    void FollowVehicle(Camera* camera);
    // Floor / Ground static collider
    void CreateOrUpdateFloor(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale);

    // Synchronize scene models with PhysX actors
    void SyncAllActorsFromScene(Renderer* renderer);
    void SyncAllActorsToScene(Renderer* renderer);
    void ClearAllActors();

    // Create / destroy / update individual actors by ModelId
    void CreateActorForModel(uint32_t modelId, bool isDynamicModel, const std::string& name,
                             const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot, const DirectX::XMFLOAT3& scale,
                             BodyType bodyType, ShapeType shapeType, float mass, float friction, float restitution,
                             float linearDamping, float angularDamping, bool useGravity, bool useCCD,
                             const ColliderSettings& collider = {});
    void ApplyModelPhysics(Renderer* renderer, uint32_t modelId, bool dynamic);
    std::string GetColliderStatus(uint32_t modelId) const;
    std::vector<DirectX::XMFLOAT3> GetColliderWireframe(uint32_t modelId) const;
    bool GetColliderBounds(uint32_t modelId, DirectX::XMFLOAT3& minimum, DirectX::XMFLOAT3& maximum) const;
    void RemoveActorForModel(uint32_t modelId);
    void SetActorTransform(uint32_t modelId, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot);
    
    // Physics interaction
    void AddForce(uint32_t modelId, const DirectX::XMFLOAT3& force);
    void AddImpulse(uint32_t modelId, const DirectX::XMFLOAT3& impulse);
    void AddExplosion(const DirectX::XMFLOAT3& origin, float radius, float strength, float upwardModifier = 0.2f);
    void ShootProjectile(Renderer* renderer, const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir,
                         float speed = 35.0f, float mass = 25.0f, float radius = 0.35f);
    void SpawnTower(Renderer* renderer, const DirectX::XMFLOAT3& basePos, int width = 3, int height = 6, int depth = 3, float boxSize = 0.8f);
    
    // Raycasting & Physics Gun / Grabber
    bool Raycast(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, float maxDist, RaycastHitInfo& outHit);

    // Simulation control
    void ResetSimulation(Renderer* renderer);
    void SetPaused(bool paused) { m_isPaused = paused; }
    bool IsPaused() const { return m_isPaused; }
    void SingleStep() { m_singleStep = true; }
    
    // Settings
    void SetGravity(const DirectX::XMFLOAT3& g);
    void ConfigureSimulation(float hz,int steps){m_fixedTimestep=1.f/(std::clamp)(hz,15.f,240.f);m_maxSubSteps=(std::clamp)(steps,1,16);m_timeAccumulator=0;}
    DirectX::XMFLOAT3 GetGravity() const;

    // PhysX accessors
    physx::PxPhysics* GetPhysics() const { return m_physics; }
    physx::PxScene* GetScene() const { return m_scene; }
    physx::PxMaterial* GetDefaultMaterial() const { return m_defaultMaterial; }

    // ImGui UI Drawing
    void DrawUI(Renderer* renderer, Camera* camera);

private:
    PhysicsSystem();
    ~PhysicsSystem();
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    struct ActorRecord {
        physx::PxRigidActor* actor = nullptr;
        uint32_t modelId = 0;
        bool isDynamicModel = false;
        std::string name;
        BodyType bodyType = BodyType::None;
        ShapeType shapeType = ShapeType::Box;
        DirectX::XMFLOAT3 initialPos = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 initialRot = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 initialScale = { 1.0f, 1.0f, 1.0f };
    };

#if defined(_WIN32)
    physx::PxDefaultAllocator m_allocator;
    physx::PxDefaultErrorCallback m_errorCallback;
#endif
    physx::PxFoundation* m_foundation = nullptr;
    physx::PxPhysics* m_physics = nullptr;
    physx::PxDefaultCpuDispatcher* m_dispatcher = nullptr;
    physx::PxScene* m_scene = nullptr;
    physx::PxMaterial* m_defaultMaterial = nullptr;
    physx::PxPvd* m_pvd = nullptr;

    physx::PxRigidStatic* m_floorActor = nullptr;
    DirectX::XMFLOAT3 m_floorPos = { 0.0f, -0.5f, 0.0f };
    DirectX::XMFLOAT3 m_floorScale = { 300.0f, 1.0f, 300.0f };

    std::unordered_map<uint32_t, ActorRecord> m_actors;
    Renderer* m_renderer = nullptr;
    struct VehicleState { float throttle=0,steer=0,brake=0,spin=0,speed=0;bool handbrake=false;float lengths[4]={.45f,.45f,.45f,.45f};int contacts=0;int manualGear=1;std::shared_ptr<VehicleDynamics> dynamics;EngineAI::VehicleRoadState road; };
    std::unordered_map<uint32_t,VehicleState> m_vehicles;
    uint32_t m_drivenVehicle=0;
    bool m_vehicleSDK=false;
    bool m_vehicleKey=false,m_vehicleGearUp=false,m_vehicleGearDown=false;
    std::unordered_map<uint32_t, std::string> m_colliderStatus;

    // Simulation timing
    bool m_isPaused = false;
    bool m_singleStep = false;
    float m_fixedTimestep = 1.0f / 60.0f;
    float m_timeAccumulator = 0.0f;
    int m_maxSubSteps = 4;
    float m_lastSimTimeMs = 0.0f;

    // Interactive Grabber (Physics Gun)
    bool m_grabberActive = false;
    physx::PxRigidDynamic* m_grabbedActor = nullptr;
    float m_grabDistance = 3.5f;
    DirectX::XMFLOAT3 m_grabLocalOffset = { 0.0f, 0.0f, 0.0f };

    // Interactive Spawner Settings
    float m_shootSpeed = 35.0f;
    float m_shootMass = 25.0f;
    float m_shootRadius = 0.35f;
    int m_shootShape = 1; // 0=Box, 1=Sphere

    float m_explosionStrength = 2000.0f;
    float m_explosionRadius = 15.0f;
    float m_explosionUpward = 0.3f;

    int m_towerWidth = 3;
    int m_towerHeight = 6;
    int m_towerDepth = 3;
    float m_towerBoxSize = 0.8f;
};

} // namespace EnginePhysics
