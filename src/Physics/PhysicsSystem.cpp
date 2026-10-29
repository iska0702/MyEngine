#include "PhysicsSystem.h"
#if defined(_WIN32)
#include "Renderer.h"
#include "Camera.h"
#include "Window.h"
#include "CharacterController.h"
#include "ColliderGeometry.h"
#include "Ragdoll.h"
#include "VehicleDynamics.h"
#include "VehicleSimulation.h"
#include "Scene/SceneGraph.h"
#include <imgui.h>
#include <chrono>
#include <cmath>

namespace EnginePhysics {

#include "Vehicle.inl"

PhysicsSystem& PhysicsSystem::Get() {
    static PhysicsSystem instance;
    return instance;
}

PhysicsSystem::PhysicsSystem() {
}

PhysicsSystem::~PhysicsSystem() {
    Shutdown();
}

bool PhysicsSystem::Initialize(Renderer* renderer) {
    LOG("PhysicsSystem::Initialize starting...");

    // 1. Create Foundation
    m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);
    if (!m_foundation) {
        LOG("PhysicsSystem ERROR: PxCreateFoundation failed!");
        return false;
    }

    // 2. Tolerances Scale
    physx::PxTolerancesScale scale;
    scale.length = 1.0f; // 1 unit = 1 meter
    scale.speed = 10.0f;

    // 3. Create Physics core
    m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, scale, true, m_pvd, nullptr);
    if (!m_physics) {
        LOG("PhysicsSystem ERROR: PxCreatePhysics failed!");
        return false;
    }

    // 4. Initialize Extensions
    if (!PxInitExtensions(*m_physics, m_pvd)) {
        LOG("PhysicsSystem WARNING: PxInitExtensions failed.");
    }

    if(!physx::PxInitVehicleExtension(*m_foundation))return false;
    m_vehicleSDK=true;
    // 6. Multi-threaded CPU dispatcher
    m_dispatcher = physx::PxDefaultCpuDispatcherCreate(2);

    // 7. Create Physics Scene
    physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = m_dispatcher;
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
    sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;

    m_scene = m_physics->createScene(sceneDesc);
    if (!m_scene) {
        LOG("PhysicsSystem ERROR: PxScene creation failed!");
        return false;
    }

    // 8. Default Physics Material (friction 0.5, restitution 0.3)
    m_defaultMaterial = m_physics->createMaterial(0.5f, 0.5f, 0.3f);

    // 9. Create Floor Static Collider
    m_renderer = renderer;

    // 10. Initialize PhysX Kinematic Character Controller
    CharacterController::Get().Initialize(m_physics, m_scene, m_defaultMaterial);

    // 11. Initial sync from scene
    if (renderer) {
        SyncAllActorsFromScene(renderer);
    }

    LOG("PhysicsSystem::Initialize SUCCESS!");
    return true;
}

void PhysicsSystem::Shutdown() {
    if(m_scene)EngineAnim::Ragdoll::ResetAll();
    CharacterController::Get().Shutdown();

    if (m_grabbedActor) {
        m_grabbedActor = nullptr;
        m_grabberActive = false;
    }

    for (auto& pair : m_actors) {
        if (pair.second.actor) {
            if (m_scene) m_scene->removeActor(*pair.second.actor);
            pair.second.actor->release();
        }
    }
    m_actors.clear();m_vehicles.clear();m_drivenVehicle=0;
    m_colliderStatus.clear();

    if (m_floorActor) {
        if (m_scene) m_scene->removeActor(*m_floorActor);
        m_floorActor->release();
        m_floorActor = nullptr;
    }

    if (m_defaultMaterial) {
        m_defaultMaterial->release();
        m_defaultMaterial = nullptr;
    }

    if (m_scene) {
        m_scene->release();
        m_scene = nullptr;
    }

    if (m_dispatcher) {
        m_dispatcher->release();
        m_dispatcher = nullptr;
    }

    if(m_vehicleSDK){physx::PxCloseVehicleExtension();m_vehicleSDK=false;}
    PxCloseExtensions();

    if (m_physics) {
        m_physics->release();
        m_physics = nullptr;
    }

    if (m_pvd) {
        m_pvd->release();
        m_pvd = nullptr;
    }

    if (m_foundation) {
        m_foundation->release();
        m_foundation = nullptr;
    }

    LOG("PhysicsSystem::Shutdown completed.");
}

void PhysicsSystem::CreateOrUpdateFloor(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& scale) {
    if (!m_physics || !m_scene) return;

    m_floorPos = pos;
    m_floorScale = scale;

    if (m_floorActor) {
        m_scene->removeActor(*m_floorActor);
        m_floorActor->release();
        m_floorActor = nullptr;
    }

    // Solid ground plane collider covering full playable area (at least 600x600 meters)
    float halfX = (std::max)(scale.x * 0.5f, 300.0f);
    float halfZ = (std::max)(scale.z * 0.5f, 300.0f);
    float halfY = (std::max)(scale.y * 0.5f, 1.0f);

    physx::PxTransform floorPose(physx::PxVec3(pos.x, pos.y - halfY, pos.z));
    m_floorActor = m_physics->createRigidStatic(floorPose);
    if (m_floorActor) {
        physx::PxBoxGeometry floorGeom(halfX, halfY, halfZ);
        physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*m_floorActor, floorGeom, *m_defaultMaterial);
        if (shape) {
            m_scene->addActor(*m_floorActor);
        }
    }
}

void PhysicsSystem::CreateActorForModel(uint32_t modelId, bool isDynamicModel, const std::string& name,
                                       const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot, const DirectX::XMFLOAT3& scale,
                                       BodyType bodyType, ShapeType shapeType, float mass, float friction, float restitution,
                                       float linearDamping, float angularDamping, bool useGravity, bool useCCD, const ColliderSettings& collider)
{
    if (!m_physics || !m_scene || modelId == 0) return;

    // Remove existing actor if any
    // Replace the previous actor only after the new collider has been created.

    if (bodyType == BodyType::None) { RemoveActorForModel(modelId); return; }

    physx::PxQuat q = EulerToPxQuat(rot);
    physx::PxTransform transform(physx::PxVec3(pos.x, pos.y, pos.z), q);

    // Create material
    physx::PxMaterial* mat = m_physics->createMaterial(friction, friction, restitution);
    if (!mat) mat = m_defaultMaterial;

    CollisionMeshData mesh;
    if (!m_renderer || !m_renderer->GetCollisionMesh(modelId, isDynamicModel, mesh)) {
        m_colliderStatus[modelId] = "Mesh data unavailable. Import or select a mesh first.";
        if (mat && mat != m_defaultMaterial) mat->release();
        return;
    }
    auto attachShape = [&](physx::PxRigidActor& target) {
        return AttachCollider(*m_physics, target, *mat, static_cast<int>(shapeType), mesh,
                              physx::PxVec3(scale.x, scale.y, scale.z), collider);
    };
    physx::PxRigidActor* actor = nullptr;

    if (bodyType == BodyType::Static) {
        physx::PxRigidStatic* staticActor = m_physics->createRigidStatic(transform);
        if (staticActor) {
            attachShape(*staticActor);
            actor = staticActor;
        }
    } else {
        // Dynamic or Kinematic
        physx::PxRigidDynamic* dynamicActor = m_physics->createRigidDynamic(transform);
        if (dynamicActor) {
            attachShape(*dynamicActor);
            
            dynamicActor->setLinearDamping(linearDamping);
            dynamicActor->setAngularDamping(angularDamping);
            dynamicActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !useGravity);

            if (useCCD) {
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
            }

            if (bodyType == BodyType::Kinematic) {
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
            } else {
                physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamicActor, (std::max)(mass, 0.1f));
            }
            actor = dynamicActor;
        }
    }

    if (mat && mat != m_defaultMaterial) {
        mat->release();
    }

    if (actor && actor->getNbShapes() == 0) {
        actor->release();
        m_colliderStatus[modelId] = "Collider could not be built. Check shape, size and mesh; triangle mesh requires Static.";
        return;
    }
    if (actor) {
        RemoveActorForModel(modelId);
        m_colliderStatus[modelId] = "Collider ready";
        actor->userData = (void*)(uintptr_t)modelId;
        m_scene->addActor(*actor);

        ActorRecord rec;
        rec.actor = actor;
        rec.modelId = modelId;
        rec.isDynamicModel = isDynamicModel;
        rec.name = name;
        rec.bodyType = bodyType;
        rec.shapeType = shapeType;
        rec.initialPos = pos;
        rec.initialRot = rot;
        rec.initialScale = scale;

        m_actors[modelId] = rec;
    }
}

void PhysicsSystem::RemoveActorForModel(uint32_t modelId) {
    EngineAnim::Ragdoll::ResetOwner(modelId);
    if (modelId == 0) return;
    m_colliderStatus.erase(modelId);
    m_vehicles.erase(modelId);if(m_drivenVehicle==modelId)m_drivenVehicle=0;

    auto it = m_actors.find(modelId);
    if (it != m_actors.end()) {
        if (it->second.actor == m_grabbedActor) {
            m_grabbedActor = nullptr;
            m_grabberActive = false;
        }

        if (it->second.actor && m_scene) {
            m_scene->removeActor(*it->second.actor);
            it->second.actor->release();
        }
        m_actors.erase(it);
    }
}

void PhysicsSystem::ClearAllActors() {
    EngineAnim::Ragdoll::ResetAll();
    if (m_grabbedActor) {
        m_grabbedActor = nullptr;
        m_grabberActive = false;
    }

    for (auto& pair : m_actors) {
        if (pair.second.actor) {
            if (m_scene) m_scene->removeActor(*pair.second.actor);
            pair.second.actor->release();
        }
    }
    m_actors.clear();m_vehicles.clear();m_drivenVehicle=0;
    m_colliderStatus.clear();
}

bool PhysicsSystem::GetColliderBounds(uint32_t modelId, DirectX::XMFLOAT3& minimum, DirectX::XMFLOAT3& maximum) const {
    auto it=m_actors.find(modelId);
    if(it==m_actors.end() || !it->second.actor) return false;
    auto bounds=it->second.actor->getWorldBounds(1.0f);
    minimum=PxToXM3(bounds.minimum); maximum=PxToXM3(bounds.maximum);
    return true;
}

std::vector<DirectX::XMFLOAT3> PhysicsSystem::GetColliderWireframe(uint32_t modelId) const {
    std::vector<DirectX::XMFLOAT3> lines;
    auto found = m_actors.find(modelId);
    if (found == m_actors.end()) return lines;
    auto* actor = found->second.actor;
    physx::PxShape* shape = nullptr;
    if (!actor || !actor->getShapes(&shape, 1) || !shape) return lines;
    const auto pose = actor->getGlobalPose() * shape->getLocalPose();
    auto edge = [&](const physx::PxVec3& a, const physx::PxVec3& b) {
        lines.push_back(PxToXM3(pose.transform(a))); lines.push_back(PxToXM3(pose.transform(b)));
    };
    const physx::PxGeometryHolder geometry(shape->getGeometry());
    if (geometry.getType() == physx::PxGeometryType::eBOX) {
        const auto h = geometry.box().halfExtents;
        for (int i=0;i<8;++i) for(int axis=0;axis<3;++axis) if (!(i&(1<<axis))) {
            int j=i|(1<<axis);
            edge({i&1?h.x:-h.x,i&2?h.y:-h.y,i&4?h.z:-h.z}, {j&1?h.x:-h.x,j&2?h.y:-h.y,j&4?h.z:-h.z});
        }
    } else if (geometry.getType() == physx::PxGeometryType::eCONVEXMESH) {
        auto* mesh = geometry.convexMesh().convexMesh;
        for (physx::PxU32 p=0;p<mesh->getNbPolygons();++p) {
            physx::PxHullPolygon polygon; mesh->getPolygonData(p,polygon);
            auto* indices = mesh->getIndexBuffer()+polygon.mIndexBase;
            for(unsigned i=0;i<polygon.mNbVerts;++i) edge(mesh->getVertices()[indices[i]],mesh->getVertices()[indices[(i+1)%polygon.mNbVerts]]);
        }
    } else if (geometry.getType() == physx::PxGeometryType::eTRIANGLEMESH) {
        auto* mesh = geometry.triangleMesh().triangleMesh;
        auto* vertices = mesh->getVertices();
        auto index = [&](unsigned i) { return mesh->getTriangleMeshFlags().isSet(physx::PxTriangleMeshFlag::e16_BIT_INDICES)
            ? static_cast<unsigned>(static_cast<const physx::PxU16*>(mesh->getTriangles())[i])
            : static_cast<const physx::PxU32*>(mesh->getTriangles())[i]; };
        for(unsigned t=0;t<(std::min)(mesh->getNbTriangles(),2048u);++t)
            for(unsigned i=0;i<3;++i) edge(vertices[index(t*3+i)],vertices[index(t*3+(i+1)%3)]);
    } else if (geometry.getType() == physx::PxGeometryType::eSPHERE || geometry.getType() == physx::PxGeometryType::eCAPSULE) {
        bool capsule = geometry.getType() == physx::PxGeometryType::eCAPSULE;
        float radius = capsule ? geometry.capsule().radius : geometry.sphere().radius;
        float half = capsule ? geometry.capsule().halfHeight : 0;
        for(int axis=0;axis<3;++axis) for(int i=0;i<32;++i) {
            float a=i*DirectX::XM_2PI/32, b=(i+1)*DirectX::XM_2PI/32;
            auto point=[&](float angle) { physx::PxVec3 p(0); p[(axis+1)%3]=radius*std::cos(angle);p[(axis+2)%3]=radius*std::sin(angle); if(capsule)p.x+=p.x>=0?half:-half;return p; };
            edge(point(a),point(b));
        }
    }
    return lines;
}

std::string PhysicsSystem::GetColliderStatus(uint32_t modelId) const {
    auto it = m_colliderStatus.find(modelId);
    return it == m_colliderStatus.end() ? "No collider" : it->second;
}

void PhysicsSystem::ApplyModelPhysics(Renderer* renderer, uint32_t modelId, bool dynamic) {
    if (!renderer) return;
    m_renderer = renderer;
    auto apply = [&](const auto& model) {
        if (!model.hasCollision || !model.visible) {
            RemoveActorForModel(modelId);
            m_colliderStatus[modelId] = "Collision disabled";
            return;
        }
        CreateActorForModel(modelId, dynamic, model.name, model.position, model.rotation, model.scale,
            model.physicsBodyType == 0 ? BodyType::Static : static_cast<BodyType>(model.physicsBodyType),
            static_cast<ShapeType>(model.physicsShapeType), model.physicsMass, model.physicsFriction,
            model.physicsRestitution, model.physicsLinearDamping, model.physicsAngularDamping,
            model.physicsEnableGravity, model.physicsEnableCCD, model.collider);
    };
    if (dynamic) { for (const auto& m : renderer->GetDynamicModels()) if (m.modelId == modelId) { apply(m); return; } }
    else { for (const auto& m : renderer->GetModels()) if (m.modelId == modelId) { apply(m); return; } }
}

void PhysicsSystem::SetActorTransform(uint32_t modelId, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& rot) {
    auto it = m_actors.find(modelId);
    if (it != m_actors.end() && it->second.actor) {
        physx::PxQuat q = EulerToPxQuat(rot);
        physx::PxTransform transform(physx::PxVec3(pos.x, pos.y, pos.z), q);
        it->second.actor->setGlobalPose(transform);

        if (it->second.bodyType == BodyType::Dynamic) {
            physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
            if (dyn) {
                dyn->setLinearVelocity(physx::PxVec3(0, 0, 0));
                dyn->setAngularVelocity(physx::PxVec3(0, 0, 0));
                dyn->wakeUp();
            }
        }
    }
}

void PhysicsSystem::AddForce(uint32_t modelId, const DirectX::XMFLOAT3& force) {
    auto it = m_actors.find(modelId);
    if (it != m_actors.end() && it->second.actor) {
        physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
        if (dyn && it->second.bodyType == BodyType::Dynamic) {
            dyn->addForce(XM3ToPx(force), physx::PxForceMode::eFORCE);
        }
    }
}

void PhysicsSystem::AddImpulse(uint32_t modelId, const DirectX::XMFLOAT3& impulse) {
    auto it = m_actors.find(modelId);
    if (it != m_actors.end() && it->second.actor) {
        physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
        if (dyn && it->second.bodyType == BodyType::Dynamic) {
            dyn->addForce(XM3ToPx(impulse), physx::PxForceMode::eIMPULSE);
        }
    }
}

void PhysicsSystem::AddExplosion(const DirectX::XMFLOAT3& origin, float radius, float strength, float upwardModifier) {
    if (!m_scene) return;

    physx::PxVec3 pOrigin = XM3ToPx(origin);

    for (auto& pair : m_actors) {
        if (!pair.second.actor || pair.second.bodyType != BodyType::Dynamic) continue;

        physx::PxRigidDynamic* dyn = pair.second.actor->is<physx::PxRigidDynamic>();
        if (!dyn) continue;

        physx::PxTransform pose = dyn->getGlobalPose();
        physx::PxVec3 diff = pose.p - pOrigin;
        float dist = diff.magnitude();

        if (dist <= radius) {
            physx::PxVec3 dir = dist > 0.001f ? diff / dist : physx::PxVec3(0, 1, 0);
            dir.y += upwardModifier;
            dir.normalize();

            float factor = (1.0f - (dist / radius));
            float impulseMagnitude = strength * factor * factor;
            dyn->addForce(dir * impulseMagnitude, physx::PxForceMode::eIMPULSE);
            dyn->wakeUp();
        }
    }
}

void PhysicsSystem::ShootProjectile(Renderer* renderer, const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir,
                                   float speed, float mass, float radius)
{
    if (!renderer || !m_scene || !m_physics) return;

    DirectX::XMVECTOR vDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&dir));
    DirectX::XMFLOAT3 fDir;
    DirectX::XMStoreFloat3(&fDir, vDir);

    DirectX::XMFLOAT3 spawnPos = {
        origin.x + fDir.x * (radius * 2.0f + 0.8f),
        origin.y + fDir.y * (radius * 2.0f + 0.8f),
        origin.z + fDir.z * (radius * 2.0f + 0.8f)
    };

    auto& models = renderer->GetModels();
    
    // Projectile recycling (max 24 in flight)
    Renderer::RenderModel* targetModel = nullptr;
    int projectileCount = 0;
    uint32_t oldestModelId = 0;

    for (auto& m : models) {
        if (m.name.rfind("Proj_", 0) == 0 && m.physicsBodyType == 2) {
            projectileCount++;
            if (oldestModelId == 0) {
                oldestModelId = m.modelId;
            }
        }
    }

    if (projectileCount >= 24 && oldestModelId != 0) {
        for (auto& m : models) {
            if (m.modelId == oldestModelId) {
                targetModel = &m;
                targetModel->position = spawnPos;
                targetModel->rotation = { 0.0f, 0.0f, 0.0f };
                targetModel->scale = { radius * 2.0f, radius * 2.0f, radius * 2.0f };
                SetActorTransform(targetModel->modelId, spawnPos, { 0.0f, 0.0f, 0.0f });
                break;
            }
        }
    } else {
        if (m_shootShape == 1) {
            renderer->SpawnSphere(spawnPos);
        } else {
            renderer->SpawnCube(spawnPos);
        }

        auto& updatedModels = renderer->GetModels();
        if (!updatedModels.empty()) {
            targetModel = &updatedModels.back();
            targetModel->name = "Proj_" + targetModel->name;
            targetModel->scale = { radius * 2.0f, radius * 2.0f, radius * 2.0f };
            targetModel->physicsBodyType = 2; // Dynamic
            targetModel->physicsShapeType = (m_shootShape == 1) ? 1 : 0;
            targetModel->physicsMass = mass;
            targetModel->physicsRestitution = 0.6f;
            targetModel->physicsFriction = 0.4f;
            targetModel->physicsEnableCCD = true;

            CreateActorForModel(targetModel->modelId, false, targetModel->name, spawnPos, { 0.0f, 0.0f, 0.0f }, targetModel->scale,
                                BodyType::Dynamic, (m_shootShape == 1) ? ShapeType::Sphere : ShapeType::Box,
                                mass, 0.4f, 0.6f, 0.05f, 0.05f, true, true);
        }
    }

    if (targetModel) {
        DirectX::XMFLOAT3 velocity = { fDir.x * speed, fDir.y * speed, fDir.z * speed };
        auto it = m_actors.find(targetModel->modelId);
        if (it != m_actors.end() && it->second.actor) {
            physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
            if (dyn) {
                dyn->setLinearVelocity(XM3ToPx(velocity));
                dyn->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
                dyn->wakeUp();
            }
        }
    }

    renderer->InvalidateRaytracingScene();
}

void PhysicsSystem::SpawnTower(Renderer* renderer, const DirectX::XMFLOAT3& basePos, int width, int height, int depth, float boxSize) {
    if (!renderer) return;

    float spacing = boxSize * 1.05f;
    float startX = basePos.x - (width - 1) * spacing * 0.5f;
    float startZ = basePos.z - (depth - 1) * spacing * 0.5f;

    for (int y = 0; y < height; ++y) {
        float posY = basePos.y + y * spacing + boxSize * 0.5f;
        for (int x = 0; x < width; ++x) {
            for (int z = 0; z < depth; ++z) {
                DirectX::XMFLOAT3 pos = {
                    startX + x * spacing + (y % 2 == 1 ? 0.05f : 0.0f),
                    posY,
                    startZ + z * spacing + (y % 2 == 1 ? 0.05f : 0.0f)
                };

                renderer->SpawnCube(pos);
                auto& models = renderer->GetModels();
                if (!models.empty()) {
                    Renderer::RenderModel* m = &models.back();
                    m->scale = { boxSize, boxSize, boxSize };
                    m->physicsBodyType = 2; // Dynamic
                    m->physicsShapeType = 0; // Box
                    m->physicsMass = 15.0f;
                    m->physicsFriction = 0.6f;
                    m->physicsRestitution = 0.2f;
                }
            }
        }
    }

    SyncAllActorsFromScene(renderer);
    renderer->InvalidateRaytracingScene();
}

bool PhysicsSystem::Raycast(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir, float maxDist, RaycastHitInfo& outHit) {
    if (!m_scene) return false;

    physx::PxVec3 pOrig = XM3ToPx(origin);
    physx::PxVec3 pDir = XM3ToPx(dir);
    pDir.normalize();

    physx::PxRaycastBuffer hit;
    bool hasHit = m_scene->raycast(pOrig, pDir, maxDist, hit);

    if (hasHit && hit.hasBlock) {
        outHit.hasHit = true;
        outHit.position = PxToXM3(hit.block.position);
        outHit.normal = PxToXM3(hit.block.normal);
        outHit.distance = hit.block.distance;
        outHit.hitActor = hit.block.actor;

        if (hit.block.actor) {
            outHit.ownerModelId = (uint32_t)(uintptr_t)hit.block.actor->userData;
            auto it = m_actors.find(outHit.ownerModelId);
            if (it != m_actors.end()) {
                outHit.isDynamicModel = it->second.isDynamicModel;
            }
        }
        return true;
    }

    outHit.hasHit = false;
    return false;
}

void PhysicsSystem::SyncAllActorsFromScene(Renderer* renderer) {
    if (!renderer || !m_physics || !m_scene) return;

    auto& models = renderer->GetModels();
    for (auto& model : models) {
if (model.visible && model.hasCollision) {
            auto it = m_actors.find(model.modelId);
            if (it == m_actors.end() && m_colliderStatus.find(model.modelId) == m_colliderStatus.end()) {
                CreateActorForModel(model.modelId, false, model.name, model.position, model.rotation, model.scale,
                                    model.physicsBodyType == 0 ? BodyType::Static : (BodyType)model.physicsBodyType, (ShapeType)model.physicsShapeType,
                                    model.physicsMass, model.physicsFriction, model.physicsRestitution,
                                    model.physicsLinearDamping, model.physicsAngularDamping,
                                    model.physicsEnableGravity, model.physicsEnableCCD, model.collider);
            }
        } else {
            RemoveActorForModel(model.modelId);
        }
    }

    auto& dynModels = renderer->GetDynamicModels();
    for (auto& dynModel : dynModels) {
        if (dynModel.visible && dynModel.hasCollision) {
            auto it = m_actors.find(dynModel.modelId);
            if (it == m_actors.end() && m_colliderStatus.find(dynModel.modelId) == m_colliderStatus.end()) {
                CreateActorForModel(dynModel.modelId, true, dynModel.name, dynModel.position, dynModel.rotation, dynModel.scale,
                                    dynModel.physicsBodyType == 0 ? BodyType::Static : (BodyType)dynModel.physicsBodyType, (ShapeType)dynModel.physicsShapeType,
                                    dynModel.physicsMass, dynModel.physicsFriction, dynModel.physicsRestitution,
                                    dynModel.physicsLinearDamping, dynModel.physicsAngularDamping,
                                    dynModel.physicsEnableGravity, dynModel.physicsEnableCCD, dynModel.collider);
            }
        } else {
            RemoveActorForModel(dynModel.modelId);
        }
    }
}

void PhysicsSystem::SyncAllActorsToScene(Renderer* renderer) {
    if (!renderer || !m_scene) return;

    bool anyMoved = false;

    for (auto& model : renderer->GetModels()) {
        if (model.physicsBodyType != 2 || !model.visible || model.isMelting) continue;

        auto it = m_actors.find(model.modelId);
        if (it != m_actors.end() && it->second.actor) {
            physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
            if (dyn && !dyn->isSleeping()) {
                physx::PxTransform pose = dyn->getGlobalPose();
                
                // Out of bounds protection (respawn above floor if fallen into void)
                if (pose.p.y < -80.0f) {
                    pose.p = physx::PxVec3(it->second.initialPos.x, 2.0f, it->second.initialPos.z);
                    dyn->setGlobalPose(pose);
                    dyn->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
                }

                model.position = PxToXM3(pose.p);
                model.rotation = QuatToEuler(pose.q);

                DirectX::XMMATRIX world = PxTransformToMatrix(pose, model.scale);
                DirectX::XMStoreFloat4x4(&model.worldMatrix, world);
                anyMoved = true;
            }
        }
    }

    for (auto& dynModel : renderer->GetDynamicModels()) {
        if (dynModel.physicsBodyType != 2 || !dynModel.visible || dynModel.isMelting) continue;

        auto it = m_actors.find(dynModel.modelId);
        if (it != m_actors.end() && it->second.actor) {
            physx::PxRigidDynamic* dyn = it->second.actor->is<physx::PxRigidDynamic>();
            if (dyn && !dyn->isSleeping()) {
                physx::PxTransform pose = dyn->getGlobalPose();
                
                if (pose.p.y < -80.0f) {
                    pose.p = physx::PxVec3(it->second.initialPos.x, 2.0f, it->second.initialPos.z);
                    dyn->setGlobalPose(pose);
                    dyn->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f));
                }

                dynModel.position = PxToXM3(pose.p);
                dynModel.rotation = QuatToEuler(pose.q);

                DirectX::XMMATRIX world = PxTransformToMatrix(pose, dynModel.scale);
                DirectX::XMStoreFloat4x4(&dynModel.worldMatrix, world);
                anyMoved = true;
            }
        }
    }

    if (anyMoved) {
        renderer->InvalidateRaytracingScene(false);
    }
}

void PhysicsSystem::Update(float dt, Renderer* renderer, Window* window, Camera* camera) {
    if (!m_scene || !renderer) return;

    UpdateVehicleInput(renderer,window,camera);UpdateVehicleVisuals(renderer);
    // Handle interactive physics gun (raycast grabber)
    if (camera && window && !IsDriving()) {
        // Toggle/Hold Grab on 'F' key or Middle Click
        bool grabKeyDown = window->IsKeyDown('F') || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        if (grabKeyDown) {
            if (!m_grabberActive) {
                RaycastHitInfo hit;
                DirectX::XMFLOAT3 camPos = camera->GetPosition();
                DirectX::XMFLOAT3 camDir = camera->GetForwardVector();

                if (Raycast(camPos, camDir, 50.0f, hit) && hit.hitActor) {
                    physx::PxRigidDynamic* dyn = hit.hitActor->is<physx::PxRigidDynamic>();
                    if (dyn && !(dyn->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
                        m_grabbedActor = dyn;
                        m_grabDistance = (std::clamp)(hit.distance, 2.0f, 20.0f);
                        m_grabberActive = true;
                        m_grabbedActor->wakeUp();
                    }
                }
            } else if (m_grabbedActor) {
                // Drag object to target point in front of camera
                DirectX::XMFLOAT3 camPos = camera->GetPosition();
                DirectX::XMFLOAT3 camDir = camera->GetForwardVector();
                physx::PxVec3 targetPos = XM3ToPx(camPos) + XM3ToPx(camDir) * m_grabDistance;

                physx::PxTransform pose = m_grabbedActor->getGlobalPose();
                physx::PxVec3 toTarget = targetPos - pose.p;

                // Velocity spring
                physx::PxVec3 currentVel = m_grabbedActor->getLinearVelocity();
                physx::PxVec3 desiredVel = toTarget * 12.0f;
                physx::PxVec3 force = (desiredVel - currentVel) * 10.0f;
                m_grabbedActor->addForce(force, physx::PxForceMode::eACCELERATION);
                m_grabbedActor->setAngularVelocity(m_grabbedActor->getAngularVelocity() * 0.95f);
                m_grabbedActor->wakeUp();
            }
        } else {
            if (m_grabberActive) {
                if (m_grabbedActor) {
                    // Give a small push when releasing
                    DirectX::XMFLOAT3 camDir = camera->GetForwardVector();
                    m_grabbedActor->addForce(XM3ToPx(camDir) * 5.0f, physx::PxForceMode::eIMPULSE);
                }
                m_grabberActive = false;
                m_grabbedActor = nullptr;
            }
        }
    }

    if (m_isPaused && !m_singleStep) {
        return;
    }

    // Apply route control after player input; the occupied vehicle always remains player-controlled.
    auto driveAI = [&](auto& models) {
        for (auto& model : models) {
            if (!model.vehicle.enabled || model.vehicle.part != 0 || !model.vehicle.aiEnabled || model.modelId == m_drivenVehicle || !model.visible || !model.hasCollision || model.physicsBodyType != 2) continue;
            auto& state = m_vehicles[model.modelId];
            const auto pose = physx::PxTransform(XM3ToPx(model.position), EulerToPxQuat(model.rotation));
            auto forward = pose.q.rotate(physx::PxVec3(0,0,1)); forward.y = 0;
            if (forward.normalize() < .001f) { SetVehicleInput(model.modelId, 0, 0, 1); continue; }
            const float yaw = std::atan2(forward.x, forward.z);
            float nearest = std::numeric_limits<float>::infinity();
            const auto right = physx::PxVec3(forward.z,0,-forward.x);
            for (float offset : {-0.7f,0.f,0.7f}) {
                auto start = pose.p + forward*((std::max)(model.scale.z*.5f,1.f)+.15f) + right*offset + physx::PxVec3(0,.3f,0);
                RaycastHitInfo hit;
                if (Raycast(PxToXM3(start), PxToXM3(forward), 45.f, hit) && hit.ownerModelId != model.modelId)
                    nearest = (std::min)(nearest, hit.distance);
            }
            const auto control = EngineAI::DriveRoad(model.vehicle,state.road,model.position.x,model.position.z,yaw,state.speed,nearest,dt);
            SetVehicleInput(model.modelId,control.throttle,control.steer,control.brake);
        }
    };
    driveAI(renderer->GetModels()); driveAI(renderer->GetDynamicModels());

    m_timeAccumulator += dt;
    int subSteps = 0;

    auto tStart = std::chrono::high_resolution_clock::now();

    while (m_timeAccumulator >= m_fixedTimestep && subSteps < m_maxSubSteps) {
        SimulateStep(m_fixedTimestep,renderer);
        m_timeAccumulator -= m_fixedTimestep;
        subSteps++;

        if (m_singleStep) {
            m_singleStep = false;
            m_timeAccumulator = 0.0f;
            break;
        }
    }

    if (m_timeAccumulator >= m_fixedTimestep) {
        m_timeAccumulator = 0.0f; // Discard spiral of death
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    m_lastSimTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

    // Synchronize simulated bodies with visual models
    SyncAllActorsToScene(renderer);UpdateVehicleVisuals(renderer);FollowVehicle(camera);
}

void PhysicsSystem::ResetSimulation(Renderer* renderer) {
    if (!renderer) return;

    for (auto& pair : m_actors) {
        SetActorTransform(pair.first, pair.second.initialPos, pair.second.initialRot);
    }
    SyncAllActorsToScene(renderer);
}

void PhysicsSystem::SetGravity(const DirectX::XMFLOAT3& g) {
    if (m_scene) {
        m_scene->setGravity(XM3ToPx(g));
    }
}

DirectX::XMFLOAT3 PhysicsSystem::GetGravity() const {
    if (m_scene) {
        return PxToXM3(m_scene->getGravity());
    }
    return { 0.0f, -9.81f, 0.0f };
}

void PhysicsSystem::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!renderer) return;

    ImGui::Begin("PhysX Physics Simulation (NVIDIA PhysX 3.4)", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "NVIDIA PhysX 3.4.2 Engine Status: Active");
    ImGui::Text("Sim Step Time: %.3f ms | Dynamic Actors: %zu | Total: %zu",
                m_lastSimTimeMs,
                std::count_if(m_actors.begin(), m_actors.end(), [](const auto& p){ return p.second.bodyType == BodyType::Dynamic; }),
                m_actors.size());

    ImGui::Separator();

    // 1. Simulation playback controls
    if (m_isPaused) {
        if (ImGui::Button(" Resume Simulation")) m_isPaused = false;
    } else {
        if (ImGui::Button(" Pause Simulation")) m_isPaused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(" Step Frame")) {
        SingleStep();
    }
    ImGui::SameLine();
    if (ImGui::Button(" Reset All Transforms")) {
        ResetSimulation(renderer);
    }

    ImGui::Separator();

    // 2. Gravity & Engine Parameters
    if (ImGui::TreeNode("Simulation Parameters & World Settings")) {
        DirectX::XMFLOAT3 g = GetGravity();
        float grav[3] = { g.x, g.y, g.z };
        if (ImGui::SliderFloat3("Gravity", grav, -30.0f, 30.0f, "%.2f m/s²")) {
            SetGravity({ grav[0], grav[1], grav[2] });
        }
        if (ImGui::Button("Standard Earth (-9.81)")) SetGravity({ 0.0f, -9.81f, 0.0f });
        ImGui::SameLine();
        if (ImGui::Button("Moon (-1.62)")) SetGravity({ 0.0f, -1.62f, 0.0f });
        ImGui::SameLine();
        if (ImGui::Button("Zero G (0.0)")) SetGravity({ 0.0f, 0.0f, 0.0f });

        float stepHz = 1.0f / m_fixedTimestep;
        if (ImGui::SliderFloat("Simulation Rate (Hz)", &stepHz, 30.0f, 240.0f, "%.0f Hz")) {
            m_fixedTimestep = 1.0f / (std::max)(stepHz, 1.0f);
        }
        ImGui::SliderInt("Max Sub-steps", &m_maxSubSteps, 1, 8);

        ImGui::TreePop();
    }

    ImGui::Separator();

    // 3. Interactive Physics Sandbox Tools
    if (ImGui::TreeNode("Interactive Tools & Projectile Cannon")) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Controls: Press 'G' to shoot projectile | Press 'F' to grab/drag with Physics Gun");

        const char* shapes[] = { "Box", "Sphere" };
        ImGui::Combo("Projectile Shape", &m_shootShape, shapes, IM_ARRAYSIZE(shapes));
        ImGui::SliderFloat("Projectile Speed", &m_shootSpeed, 5.0f, 100.0f, "%.1f m/s");
        ImGui::SliderFloat("Projectile Mass", &m_shootMass, 1.0f, 200.0f, "%.1f kg");
        ImGui::SliderFloat("Projectile Radius", &m_shootRadius, 0.1f, 2.0f, "%.2f m");

        if (camera && ImGui::Button(" Shoot Projectile from Camera (or Press G)")) {
            ShootProjectile(renderer, camera->GetPosition(), camera->GetForwardVector(), m_shootSpeed, m_shootMass, m_shootRadius);
        }

        ImGui::Separator();
        ImGui::Text("Destructible Tower Spawner:");
        ImGui::SliderInt("Width", &m_towerWidth, 1, 6);
        ImGui::SliderInt("Height", &m_towerHeight, 1, 15);
        ImGui::SliderInt("Depth", &m_towerDepth, 1, 6);
        ImGui::SliderFloat("Box Size", &m_towerBoxSize, 0.3f, 2.0f, "%.2f m");

        if (camera && ImGui::Button(" Spawn Destructible Tower at Target")) {
            DirectX::XMFLOAT3 spawnPos = camera->GetPosition();
            DirectX::XMFLOAT3 forward = camera->GetForwardVector();
            spawnPos.x += forward.x * 6.0f;
            spawnPos.z += forward.z * 6.0f;
            spawnPos.y = 0.0f;
            SpawnTower(renderer, spawnPos, m_towerWidth, m_towerHeight, m_towerDepth, m_towerBoxSize);
        }

        ImGui::Separator();
        ImGui::Text("Radial Shockwave Explosion:");
        ImGui::SliderFloat("Explosion Strength", &m_explosionStrength, 100.0f, 10000.0f, "%.0f N");
        ImGui::SliderFloat("Explosion Radius", &m_explosionRadius, 2.0f, 50.0f, "%.1f m");
        ImGui::SliderFloat("Upward Lift", &m_explosionUpward, 0.0f, 1.0f, "%.2f");

        if (camera && ImGui::Button(" Detonate Shockwave in Front of Camera")) {
            DirectX::XMFLOAT3 pos = camera->GetPosition();
            DirectX::XMFLOAT3 fwd = camera->GetForwardVector();
            pos.x += fwd.x * 5.0f;
            pos.y += fwd.y * 5.0f;
            pos.z += fwd.z * 5.0f;
            AddExplosion(pos, m_explosionRadius, m_explosionStrength, m_explosionUpward);
        }

        ImGui::TreePop();
    }

    ImGui::End();
#endif
}

} // namespace EnginePhysics
#else
#include "Renderer.h"

namespace EnginePhysics {

PhysicsSystem& PhysicsSystem::Get() {
    static PhysicsSystem instance;
    return instance;
}

PhysicsSystem::PhysicsSystem() = default;
PhysicsSystem::~PhysicsSystem() { Shutdown(); }

bool PhysicsSystem::Initialize(Renderer*) {
    LOG("PhysicsSystem (macOS Metal fallback): initialized");
    return true;
}

void PhysicsSystem::Shutdown() {}
void PhysicsSystem::CreateOrUpdateFloor(const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&) {}
void PhysicsSystem::CreateActorForModel(uint32_t, bool, const std::string&, const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&, BodyType, ShapeType, float, float, float, float, float, bool, bool, const ColliderSettings&) {}
void PhysicsSystem::RemoveActorForModel(uint32_t) {}
void PhysicsSystem::ApplyModelPhysics(Renderer*, uint32_t, bool) {}
bool PhysicsSystem::GetColliderBounds(uint32_t, DirectX::XMFLOAT3&, DirectX::XMFLOAT3&) const { return false; }
std::string PhysicsSystem::GetColliderStatus(uint32_t) const { return "Physics unavailable"; }
std::vector<DirectX::XMFLOAT3> PhysicsSystem::GetColliderWireframe(uint32_t) const { return {}; }
void PhysicsSystem::SetActorTransform(uint32_t, const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&) {}
void PhysicsSystem::AddForce(uint32_t, const DirectX::XMFLOAT3&) {}
void PhysicsSystem::AddImpulse(uint32_t, const DirectX::XMFLOAT3&) {}
void PhysicsSystem::AddExplosion(const DirectX::XMFLOAT3&, float, float, float) {}
void PhysicsSystem::ShootProjectile(Renderer*, const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&, float, float, float) {}
void PhysicsSystem::SpawnTower(Renderer*, const DirectX::XMFLOAT3&, int, int, int, float) {}
bool PhysicsSystem::Raycast(const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&, float, RaycastHitInfo&) { return false; }
float PhysicsSystem::VehicleRPM(uint32_t)const{return 0;}
int PhysicsSystem::VehicleGear(uint32_t)const{return 0;}
float PhysicsSystem::VehicleSlip(uint32_t)const{return 0;}
void PhysicsSystem::SetVehicleGear(uint32_t,int){}
uint32_t PhysicsSystem::SpawnVehicle(Renderer*,const DirectX::XMFLOAT3&){return 0;}
void PhysicsSystem::SetVehicleInput(uint32_t,float,float,float,bool){}
void PhysicsSystem::StepVehicles(float,Renderer*){}
void PhysicsSystem::SimulateStep(float,Renderer*){}
void PhysicsSystem::UpdateVehicleVisuals(Renderer*){}
void PhysicsSystem::UpdateVehicleInput(Renderer*,Window*,Camera*){}
void PhysicsSystem::FollowVehicle(Camera*){}
void PhysicsSystem::SyncAllActorsFromScene(Renderer*) {}
void PhysicsSystem::SyncAllActorsToScene(Renderer*) {}
void PhysicsSystem::ClearAllActors() {}
void PhysicsSystem::Update(float, Renderer*, Window*, Camera*) {}
void PhysicsSystem::ResetSimulation(Renderer*) {}
void PhysicsSystem::SetGravity(const DirectX::XMFLOAT3&) {}
DirectX::XMFLOAT3 PhysicsSystem::GetGravity() const { return { 0.0f, -9.81f, 0.0f }; }
void PhysicsSystem::DrawUI(Renderer*, Camera*) {}

} // namespace EnginePhysics
#endif
