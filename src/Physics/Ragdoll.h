#pragma once
#if defined(_WIN32)
#include "PhysicsSystem.h"
#include "Animation/BoneClassifierAI.h"
#include "Core/Project.h"
#include <unordered_set>
#include "RagdollSettings.h"
namespace EngineAnim {
class Ragdoll {
public:
    ~Ragdoll(){Reset();instances.erase(std::remove(instances.begin(),instances.end(),this),instances.end());}
    Ragdoll(){instances.push_back(this);}
    static void ResetAll(){for(auto* instance:instances)instance->Reset();}
    static void ResetOwner(uint32_t modelId){for(auto* instance:instances)if(instance->ownerModelId==modelId)instance->Reset();}
    Ragdoll(const Ragdoll&)=delete;
    Ragdoll& operator=(const Ragdoll&)=delete;
    bool Active() const{return !bodies.empty();}
    void Reset(){for(auto* joint:joints)joint->release();joints.clear();for(auto& body:bodies)body.actor->release();bodies.clear();ownerModelId=0;status="Ragdoll stopped";}
    static EnginePhysics::RagdollSettings EffectiveSettings(EnginePhysics::RagdollSettings settings){
        if(settings.useProjectDefaults){auto t=EngineProject::active?EngineProject::current.tuning:EngineProject::ProjectTuning{};settings.mass=t.ragdollMass;settings.radius=t.ragdollRadius;settings.angularDamping=t.ragdollDamping;settings.jointLimit=t.ragdollLimit;}
        settings.Sanitize();return settings;
    }
    void ApplySettings(EnginePhysics::RagdollSettings settings){
        settings=EffectiveSettings(settings);
        for(auto& body:bodies){
            physx::PxShape* shape=nullptr;if(body.actor->getShapes(&shape,1)==1)shape->setGeometry(physx::PxSphereGeometry(settings.radius));
            physx::PxRigidBodyExt::setMassAndUpdateInertia(*body.actor,settings.mass/float(bodies.size()));
            body.actor->setLinearDamping(settings.linearDamping);body.actor->setAngularDamping(settings.angularDamping);
            body.actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY,!settings.gravity);
            body.actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD,settings.ccd);body.actor->wakeUp();
        }
        float limit=settings.jointLimit*physx::PxPi/180.f;
        for(auto* joint:joints){joint->setSwingLimit(physx::PxJointLimitCone(limit,limit));joint->setTwistLimit(physx::PxJointAngularLimitPair(-limit,limit));}
    }
    bool Start(const OzzSkeleton& skeleton,const HumanoidBoneMapping& mapping,const std::vector<Matrix4x4>& pose,DirectX::FXMMATRIX world,const EnginePhysics::RagdollSettings& settings={}){
        using namespace physx;using namespace DirectX;
        Reset();status.clear();
        auto& system=EnginePhysics::PhysicsSystem::Get();auto* physics=system.GetPhysics();auto* scene=system.GetScene();
        if(!physics||!scene||pose.size()!=size_t(skeleton.GetNumJoints())){status="Physics or skeleton pose unavailable";return false;}
        std::unordered_set<int> selected;
        for(auto [bone,index]:mapping.boneToJoint){if(bone>=HumanoidBone::Hips&&bone<=HumanoidBone::RightToes&&bone!=HumanoidBone::LeftEye&&bone!=HumanoidBone::RightEye&&bone!=HumanoidBone::Jaw&&index>=0&&index<skeleton.GetNumJoints())selected.insert(index);}
        if(selected.size()<5){status="Map at least five humanoid bones in Bone Mapping first";return false;}
        auto config=EffectiveSettings(settings);
        initialPose=pose;modelWorld=Matrix4x4(world);parents.clear();
        for(int i=0;i<skeleton.GetNumJoints();++i)parents.push_back(skeleton.GetParentIndex(i));
        for(int index:selected){
            XMVECTOR scale,rotation,position;
            if(!XMMatrixDecompose(&scale,&rotation,&position,pose[index].ToXM()*world)){Reset();status="Invalid bone transform";return false;}
            XMFLOAT4 q;XMFLOAT3 p;XMStoreFloat4(&q,rotation);XMStoreFloat3(&p,position);
            PxTransform transform(PxVec3(p.x,p.y,p.z),PxQuat(q.x,q.y,q.z,q.w));
            auto* actor=physics->createRigidDynamic(transform);
            auto* shape=physics->createShape(PxSphereGeometry(config.radius),*system.GetDefaultMaterial(),true);
            if(!actor||!shape){if(actor)actor->release();if(shape)shape->release();Reset();status="Ragdoll body creation failed";return false;}
            actor->attachShape(*shape);shape->release();
            PxRigidBodyExt::setMassAndUpdateInertia(*actor,config.mass/float(selected.size()));
            actor->setLinearDamping(config.linearDamping);actor->setAngularDamping(config.angularDamping);actor->setSolverIterationCounts(12,4);
            actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY,!config.gravity);
            actor->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD,config.ccd);
            scene->addActor(*actor);bodies.push_back({index,actor,transform});
        }
        for(auto& body:bodies){
            int parent=parents[body.bone];Body* ancestor=nullptr;
            while(parent>=0&&!ancestor){for(auto& candidate:bodies)if(candidate.bone==parent){ancestor=&candidate;break;}if(!ancestor)parent=parents[parent];}
            if(!ancestor)continue;
            PxTransform anchor(body.initial.p);
            auto* joint=PxD6JointCreate(*physics,ancestor->actor,ancestor->initial.getInverse()*anchor,body.actor,body.initial.getInverse()*anchor);
            if(!joint){Reset();status="Ragdoll joint creation failed";return false;}
            for(auto axis:{PxD6Axis::eTWIST,PxD6Axis::eSWING1,PxD6Axis::eSWING2})joint->setMotion(axis,PxD6Motion::eLIMITED);
            float limit=config.jointLimit*PxPi/180.f;
            joint->setSwingLimit(PxJointLimitCone(limit,limit));joint->setTwistLimit(PxJointAngularLimitPair(-limit,limit));
            joints.push_back(joint);
        }
        status="Ragdoll active: "+std::to_string(bodies.size())+" bodies";return true;
    }
    void Impulse(const DirectX::XMFLOAT3& impulse){for(auto& body:bodies)body.actor->addForce(physx::PxVec3(impulse.x,impulse.y,impulse.z)/float(bodies.size()),physx::PxForceMode::eIMPULSE);}
    std::vector<Matrix4x4> Pose() const {
        using namespace DirectX;
        auto result=initialPose;auto world=modelWorld.ToXM();auto inverseWorld=XMMatrixInverse(nullptr,world);
        for(size_t i=0;i<result.size();++i){
            int ancestor=(int)i;const Body* body=nullptr;
            while(ancestor>=0&&!body){for(auto& candidate:bodies)if(candidate.bone==ancestor){body=&candidate;break;}if(!body)ancestor=parents[ancestor];}
            if(body){auto before=EnginePhysics::PxTransformToMatrix(body->initial,{1,1,1});auto after=EnginePhysics::PxTransformToMatrix(body->actor->getGlobalPose(),{1,1,1});result[i]=Matrix4x4(initialPose[i].ToXM()*world*XMMatrixInverse(nullptr,before)*after*inverseWorld);}
        }
        return result;
    }
    std::string status;
    uint32_t ownerModelId=0;
    DirectX::XMFLOAT3 modelPosition{},modelRotation{},modelScale{1,1,1};
private:
    // Initialized before function-static engine systems; remains alive during their shutdown.
    static inline std::vector<Ragdoll*> instances;
    struct Body{int bone;physx::PxRigidDynamic* actor;physx::PxTransform initial;};
    std::vector<Body> bodies;std::vector<physx::PxD6Joint*> joints;
    std::vector<int> parents;std::vector<Matrix4x4> initialPose;Matrix4x4 modelWorld;
};
}
#endif
