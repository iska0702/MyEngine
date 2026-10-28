#pragma once
#include "ColliderData.h"
#include <PxPhysicsAPI.h>
#include <cooking/PxCooking.h>
#include <algorithm>
#include <cmath>

namespace EnginePhysics {
inline physx::PxShape* AttachCollider(physx::PxPhysics& physics, physx::PxRigidActor& actor,
    physx::PxMaterial& material, int shapeType, const CollisionMeshData& mesh,
    const physx::PxVec3& modelScale, const ColliderSettings& settings = {}) {
    using namespace physx;
    PxVec3 size(settings.size.x, settings.size.y, settings.size.z);
    if (!modelScale.isFinite() || !size.isFinite() || size.minElement() <= 0 ||
        PxAbs(modelScale.x) < 0.0001f || PxAbs(modelScale.y) < 0.0001f || PxAbs(modelScale.z) < 0.0001f)
        return nullptr;
    PxVec3 offset(settings.offset.x * modelScale.x, settings.offset.y * modelScale.y, settings.offset.z * modelScale.z);
    if (!offset.isFinite()) return nullptr;
    std::vector<PxVec3> points;
    for (const auto& v : mesh.vertices) {
        PxVec3 p(v.x * modelScale.x, v.y * modelScale.y, v.z * modelScale.z);
        if (!p.isFinite()) return nullptr;
        points.push_back(p);
    }
    if (points.empty()) {
        for (int i=0; i<8; ++i) points.emplace_back((i&1 ? .5f : -.5f)*modelScale.x,
            (i&2 ? .5f : -.5f)*modelScale.y, (i&4 ? .5f : -.5f)*modelScale.z);
    }
    PxVec3 lo = points[0], hi = points[0];
    for (const auto& p : points) { lo = lo.minimum(p); hi = hi.maximum(p); }
    const PxVec3 center = (lo + hi) * .5f;
    PxVec3 half = (hi - lo).multiply(size) * .5f;
    half.x = (std::max)(half.x, .025f); half.y = (std::max)(half.y, .025f); half.z = (std::max)(half.z, .025f);
    for (auto& p : points) p = center + (p-center).multiply(size) + offset;
    PxShape* shape = nullptr;
    if (shapeType == 4) {
        PxConvexMeshDesc desc;
        desc.points.count = static_cast<PxU32>(points.size()); desc.points.stride = sizeof(PxVec3); desc.points.data = points.data();
        desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
        auto* cooked = PxCreateConvexMesh(PxCookingParams(physics.getTolerancesScale()), desc, physics.getPhysicsInsertionCallback());
        if (cooked) { shape = PxRigidActorExt::createExclusiveShape(actor, PxConvexMeshGeometry(cooked), material); cooked->release(); }
        return shape;
    }
    if (shapeType == 5) {
        if (!actor.is<PxRigidStatic>() || mesh.indices.empty() || mesh.indices.size()%3) return nullptr;
        for (auto i : mesh.indices) if (i >= points.size()) return nullptr;
        PxTriangleMeshDesc desc;
        desc.points.count = static_cast<PxU32>(points.size()); desc.points.stride = sizeof(PxVec3); desc.points.data = points.data();
        desc.triangles.count = static_cast<PxU32>(mesh.indices.size()/3); desc.triangles.stride = sizeof(uint32_t)*3; desc.triangles.data = mesh.indices.data();
        auto* cooked = PxCreateTriangleMesh(PxCookingParams(physics.getTolerancesScale()), desc, physics.getPhysicsInsertionCallback());
        if (cooked) { shape = PxRigidActorExt::createExclusiveShape(actor, PxTriangleMeshGeometry(cooked), material); cooked->release(); }
        return shape;
    }
    PxTransform local(center + offset);
    if (shapeType != 1 && shapeType != 2 && hi.y-lo.y < .001f) local.p.y -= half.y;
    if (shapeType == 1) shape = PxRigidActorExt::createExclusiveShape(actor, PxSphereGeometry(half.maxElement()), material);
    else if (shapeType == 2) {
        float radius = (std::min)((std::max)(half.x,half.z),half.y);
        shape = PxRigidActorExt::createExclusiveShape(actor, PxCapsuleGeometry(radius,(std::max)(half.y-radius, .001f)), material);
        local.q = PxQuat(PxHalfPi, PxVec3(0,0,1));
    } else shape = PxRigidActorExt::createExclusiveShape(actor, PxBoxGeometry(half), material);
    if (shape) shape->setLocalPose(local);
    return shape;
}
}
