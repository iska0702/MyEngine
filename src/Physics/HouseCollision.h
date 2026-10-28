#pragma once
#include <PxPhysicsAPI.h>
#include <cooking/PxCooking.h>
#include <string>

namespace EnginePhysics {
// Local coordinates match the built-in house meshes (origin is at ground level).
inline physx::PxShape* AttachHouseCollision(physx::PxPhysics& physics, physx::PxRigidActor& actor,
    physx::PxMaterial& material, const std::string& name, const physx::PxVec3& scale) {
    using namespace physx;
    if (name == "House Walls") {
        auto* shape = PxRigidActorExt::createExclusiveShape(actor,
            PxBoxGeometry(PxAbs(scale.x) * 3.0f, PxAbs(scale.y) * 1.75f, PxAbs(scale.z) * 3.5f), material);
        if (shape) shape->setLocalPose(PxTransform(PxVec3(0, 1.75f * scale.y, 0)));
        return shape;
    }
    if (name != "House Roof") return nullptr;
    const PxVec3 points[] = {
        {-3.4f * scale.x, 3.45f * scale.y, -3.9f * scale.z},
        { 3.4f * scale.x, 3.45f * scale.y, -3.9f * scale.z},
        { 3.4f * scale.x, 3.45f * scale.y,  3.9f * scale.z},
        {-3.4f * scale.x, 3.45f * scale.y,  3.9f * scale.z},
        {0, 5.7f * scale.y, 0}
    };
    PxConvexMeshDesc desc;
    desc.points.count = 5;
    desc.points.stride = sizeof(PxVec3);
    desc.points.data = points;
    desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
    auto* mesh = PxCreateConvexMesh(PxCookingParams(physics.getTolerancesScale()), desc,
                                  physics.getPhysicsInsertionCallback());
    if (!mesh) return nullptr;
    auto* shape = PxRigidActorExt::createExclusiveShape(actor, PxConvexMeshGeometry(mesh), material);
    mesh->release();
    return shape;
}
}
