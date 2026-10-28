#include "Physics/CharacterController.h"
#include "Renderer/Camera.h"
#include "Physics/ColliderGeometry.h"
#include "Physics/PhysicsSystem.h"
#include "Rain/RainVisibility.h"
#include <PxPhysicsAPI.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int RunRegression() {
    using namespace physx;
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    auto* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors);
    Require(foundation != nullptr, "foundation");
    auto* physics = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, PxTolerancesScale());
    Require(physics != nullptr, "physics");
    auto* dispatcher = PxDefaultCpuDispatcherCreate(1);
    PxSceneDesc desc(physics->getTolerancesScale());
    desc.gravity = PxVec3(0, -20, 0);
    desc.cpuDispatcher = dispatcher;
    desc.filterShader = PxDefaultSimulationFilterShader;
    auto* scene = physics->createScene(desc);
    Require(scene != nullptr, "scene");
    auto* material = physics->createMaterial(0.5f, 0.5f, 0.0f);
    auto* floor = PxCreatePlane(*physics, PxPlane(0, 1, 0, 0), *material);
    scene->addActor(*floor);
    auto& controller = EnginePhysics::CharacterController::Get();
    Require(controller.Initialize(physics, scene, material), "controller");
    controller.SetPosition(0, 2, -5);
    Camera camera;
    auto step = [&](float dt, bool crouch, bool jump = false) {
        controller.Update(dt, {0,0,0}, jump, false, crouch);
        camera.SetControllerPose(controller.GetEyePosition(), controller.IsGrounded());
        camera.Update(dt, false);
        Require(std::abs(camera.GetPosition().y - controller.GetEyePosition().y) < 0.00001f,
                "camera applied gravity after PhysX");
        scene->simulate(dt);
        scene->fetchResults(true);
    };
    for (int i = 0; i < 120; ++i) step(1.0f/60, false);
    const float standing = camera.GetPosition().y;
    float low = standing, high = standing;
    for (int i = 0; i < 600; ++i) {
        step(i % 3 == 0 ? 1.0f/30 : 1.0f/144, false);
        low = (std::min)(low, camera.GetPosition().y);
        high = (std::max)(high, camera.GetPosition().y);
    }
    Require(high - low < 0.002f, "idle height oscillation");
    const float center = controller.GetPosition().y;
    const float halfCylinder = controller.GetConfig().height * 0.25f;
    step(1.0f/60, true);
    Require(std::abs(controller.GetPosition().y - (center - halfCylinder)) < 0.002f,
            "crouching moved the feet");
    const float crouched = camera.GetPosition().y;
    for (int i = 0; i < 120; ++i) step(1.0f/60, true);
    Require(std::abs(camera.GetPosition().y - crouched) < 0.002f, "crouch settling drop");
    step(1.0f/60, false);
    Require(std::abs(camera.GetPosition().y - standing) < 0.002f, "standing height changed");
    step(1.0f/60, false, true);
    Require(camera.GetPosition().y > standing, "jump did not rise");
    for (int i = 0; i < 180; ++i) step(1.0f/60, false);
    Require(std::abs(camera.GetPosition().y - standing) < 0.002f, "landing height changed");
    Camera fallback;
    fallback.SetPosition(0, 3, 0);
    fallback.Update(0.1f);
    Require(fallback.GetPosition().y < 3, "fallback gravity disabled");
    auto* walls = physics->createRigidStatic(PxTransform(PxVec3(0,0,8)));
    auto* roof = physics->createRigidStatic(PxTransform(PxVec3(0,0,8)));
    EnginePhysics::CollisionMeshData wallsMesh, roofMesh;
    for(int i=0;i<8;++i) wallsMesh.vertices.push_back({i&1?3.0f:-3.0f,i&2?3.5f:0,i&4?3.5f:-3.5f});
    roofMesh.vertices = {{-3.4f,3.45f,-3.9f},{3.4f,3.45f,-3.9f},{3.4f,3.45f,3.9f},{-3.4f,3.45f,3.9f},{0,5.7f,0}};
    Require(EnginePhysics::AttachCollider(*physics, *walls, *material, 0, wallsMesh, PxVec3(1)) != nullptr, "mesh-fitted walls shape");
    Require(EnginePhysics::AttachCollider(*physics, *roof, *material, 4, roofMesh, PxVec3(1)) != nullptr, "generic convex roof shape");
    scene->addActor(*walls);
    scene->addActor(*roof);
    PxRaycastBuffer roofHit;
    Require(scene->raycast(PxVec3(1,10,8), PxVec3(0,-1,0), 10, roofHit), "roof raycast");
    Require(roofHit.block.actor == roof && std::abs(roofHit.block.position.y - (5.7f-2.25f/3.4f)) < 0.01f,
            "roof collision does not follow the slope");
    for (int i=0; i<180; ++i) {
        controller.Update(1.0f/60, {0,0,1}, false, false, false);
        scene->simulate(1.0f/60);
        scene->fetchResults(true);
    }
    Require(controller.GetPosition().z < 4.2f, "character passed through house wall");
    Require(controller.GetPosition().z > 3.8f, "house collider is oversized");
    const auto angles=DirectX::XMFLOAT3(20,45,15);
    auto q=EnginePhysics::EulerToPxQuat(angles);
    auto back=EnginePhysics::QuatToEuler(q);
    Require(std::abs(back.x-angles.x)<.001f && std::abs(back.y-angles.y)<.001f && std::abs(back.z-angles.z)<.001f,"editor rotation units or axes differ from physics");
    auto* adjusted=physics->createRigidStatic(PxTransform(PxIdentity));
    EnginePhysics::ColliderSettings settings;settings.offset={1,2,3};settings.size={2,1,1};
    auto* adjustedShape=EnginePhysics::AttachCollider(*physics,*adjusted,*material,0,wallsMesh,PxVec3(1),settings);
    Require(adjustedShape!=nullptr,"configured collider");
    const PxGeometryHolder g(adjustedShape->getGeometry());
    Require(std::abs(g.box().halfExtents.x-6)<.001f && std::abs(adjustedShape->getLocalPose().p.y-3.75f)<.001f,"collider size or offset ignored");
    adjusted->release();
    Require(EngineRain::RainVolumeOpacity(0,0,0,0,26) == 1.0f, "rain center faded");
    Require(EngineRain::RainVolumeOpacity(26,0,0,0,26) == 0.0f, "rain volume edge visible");
    Require(EngineRain::RainVolumeOpacity(26,26,0,0,26) == 0.0f, "square rain corner visible");
    const float fade = EngineRain::RainVolumeOpacity(26*0.825f,0,0,0,26);
    Require(fade > 0.49f && fade < 0.51f, "rain boundary not smooth");
    walls->release();
    roof->release();
    controller.Shutdown();
    floor->release();
    material->release();
    scene->release();
    dispatcher->release();
    physics->release();
    foundation->release();
    std::cout << "Character/camera regression passed; idle height range " << high-low << " m\n";
    return 0;
}
int main() {
    try { return RunRegression(); }
    catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1; }
}
