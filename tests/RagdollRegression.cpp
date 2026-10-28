#include "Physics/Ragdoll.h"
#include <iostream>
using namespace physx;
namespace EnginePhysics {
PhysicsSystem& PhysicsSystem::Get(){static PhysicsSystem value;return value;}
PhysicsSystem::PhysicsSystem()=default;
PhysicsSystem::~PhysicsSystem(){Shutdown();}
bool PhysicsSystem::Initialize(Renderer*){
    m_foundation=PxCreateFoundation(PX_PHYSICS_VERSION,m_allocator,m_errorCallback);
    if(!m_foundation)return false;
    m_physics=PxCreatePhysics(PX_PHYSICS_VERSION,*m_foundation,PxTolerancesScale());
    if(!m_physics)return false;
    PxInitExtensions(*m_physics,nullptr);
    m_dispatcher=PxDefaultCpuDispatcherCreate(1);
    PxSceneDesc desc(m_physics->getTolerancesScale());desc.gravity={0,-9.81f,0};desc.cpuDispatcher=m_dispatcher;desc.filterShader=PxDefaultSimulationFilterShader;desc.flags|=PxSceneFlag::eENABLE_CCD;
    m_scene=m_physics->createScene(desc);m_defaultMaterial=m_physics->createMaterial(.5f,.5f,.1f);
    return m_scene&&m_defaultMaterial;
}
void PhysicsSystem::Shutdown(){if(m_defaultMaterial){m_defaultMaterial->release();m_defaultMaterial=nullptr;}if(m_scene){m_scene->release();m_scene=nullptr;}if(m_dispatcher){m_dispatcher->release();m_dispatcher=nullptr;}if(m_physics){PxCloseExtensions();m_physics->release();m_physics=nullptr;}if(m_foundation){m_foundation->release();m_foundation=nullptr;}}
}
namespace EngineAnim {
void OzzSkeleton::SetJoints(std::vector<OzzJoint> joints){m_joints=std::move(joints);}
}
int main(){
    using namespace EngineAnim;using namespace DirectX;
    auto& physics=EnginePhysics::PhysicsSystem::Get();if(!physics.Initialize(nullptr))return 1;
    OzzSkeleton skeleton;std::vector<OzzJoint> bones(5);std::vector<Matrix4x4> pose;
    HumanoidBoneMapping mapping;
    for(int i=0;i<5;++i){bones[i].parentIndex=i-1;pose.emplace_back(XMMatrixTranslation(0,2+i*.25f,0));mapping.SetMapping(static_cast<HumanoidBone>(int(HumanoidBone::Hips)+i),i);}
    skeleton.SetJoints(bones);Ragdoll doll;
    auto* floor=PxCreatePlane(*physics.GetPhysics(),PxPlane(0,1,0,0),*physics.GetDefaultMaterial());
    if(!floor)return 12;physics.GetScene()->addActor(*floor);
    if(!doll.Start(skeleton,mapping,pose,XMMatrixIdentity())){std::cerr<<doll.status;return 2;}
    PxActor* actors[8]{};auto count=physics.GetScene()->getActors(PxActorTypeFlag::eRIGID_DYNAMIC,actors,8);
    if(count!=5)return 3;
    float mass=0;for(unsigned i=0;i<count;++i)mass+=static_cast<PxRigidDynamic*>(actors[i])->getMass();
    if(std::abs(mass-70.f)>.01f)return 4;
    doll.Impulse({0,70,0});
    for(int i=0;i<60;++i){physics.GetScene()->simulate(1.f/60);physics.GetScene()->fetchResults(true);}
    auto result=doll.Pose();if(result.size()!=5)return 5;
    for(const auto& matrix:result)for(auto& row:matrix.m)for(float value:row)if(!std::isfinite(value))return 6;
    if(result[0].m[3][1]>=pose[0].m[3][1]-.5f)return 7;
    for(int i=0;i<120;++i){physics.GetScene()->simulate(1.f/60);physics.GetScene()->fetchResults(true);}
    for(auto& matrix:doll.Pose())if(matrix.m[3][1]<.02f)return 13;
    doll.Reset();if(physics.GetScene()->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC)!=0)return 8;
    if(!doll.Start(skeleton,mapping,pose,XMMatrixScaling(2,2,2)*XMMatrixRotationY(.7f)*XMMatrixTranslation(10,3,-4)))return 10;
    auto transformed=doll.Pose();
    for(size_t i=0;i<pose.size();++i)for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(std::abs(transformed[i].m[r][c]-pose[i].m[r][c])>.0001f)return 11;
    EnginePhysics::RagdollSettings custom;custom.useProjectDefaults=false;custom.mass=125;custom.radius=.12f;custom.gravity=false;custom.ccd=false;custom.angularDamping=2;custom.linearDamping=1;
    doll.ApplySettings(custom);mass=0;
    count=physics.GetScene()->getActors(PxActorTypeFlag::eRIGID_DYNAMIC,actors,8);
    for(unsigned i=0;i<count;++i){auto* actor=static_cast<PxRigidDynamic*>(actors[i]);mass+=actor->getMass();if(!(actor->getActorFlags()&PxActorFlag::eDISABLE_GRAVITY)||actor->getLinearDamping()!=1||actor->getAngularDamping()!=2||(actor->getRigidBodyFlags()&PxRigidBodyFlag::eENABLE_CCD))return 18;}
    if(std::abs(mass-125)>.01f)return 19;
    doll.ownerModelId=42;Ragdoll::ResetOwner(43);if(!doll.Active())return 14;
    Ragdoll::ResetOwner(42);if(doll.Active())return 15;
    if(!doll.Start(skeleton,mapping,pose,XMMatrixIdentity()))return 16;
    Ragdoll::ResetAll();if(doll.Active()||physics.GetScene()->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC)!=0)return 17;
    HumanoidBoneMapping empty;if(doll.Start(skeleton,empty,pose,XMMatrixIdentity()))return 9;
    floor->release();
    std::cout<<"Ragdoll: total mass, constrained fall, impulse, finite skin pose, cleanup and missing mapping passed\n";
}
