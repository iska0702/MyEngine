#pragma once
#include "VehicleDynamics.h"
#include <cooking/PxCooking.h>
#include <iostream>
#include <memory>
#include <vector>
namespace EnginePhysics {
struct VehicleGroundFilter final : physx::PxQueryFilterCallback {
    const physx::PxRigidActor* self;
    explicit VehicleGroundFilter(const physx::PxRigidActor* actor):self(actor){}
    physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&,const physx::PxShape* shape,const physx::PxRigidActor* actor,physx::PxHitFlags&) override {
        return actor==self||!(shape->getFlags()&physx::PxShapeFlag::eSIMULATION_SHAPE)?physx::PxQueryHitType::eNONE:physx::PxQueryHitType::eBLOCK;
    }
    physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&,const physx::PxQueryHit&,const physx::PxShape*,const physx::PxRigidActor*) override {return physx::PxQueryHitType::eBLOCK;}
};
// Keep chassis contacts and tire integration on the same bounded 240 Hz clock.
// Existing external forces persist through inner steps; vehicle impulses do not accumulate.
template<class Step> void AdvanceVehicleScene(physx::PxScene& scene,float dt,Step&& step){
    using namespace physx;
    if(!std::isfinite(dt)||dt<=0)return;
    const int count=(std::clamp)(int(std::ceil(dt*240.f-.0001f)),1,32);
    const float h=dt/count;
    std::vector<PxActor*> actors(scene.getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC));
    scene.getActors(PxActorTypeFlag::eRIGID_DYNAMIC,actors.data(),PxU32(actors.size()));
    std::vector<PxRigidDynamic*> retained;
    for(auto actor:actors){auto body=actor->is<PxRigidDynamic>();
        if(!(body->getRigidBodyFlags()&(PxRigidBodyFlag::eKINEMATIC|PxRigidBodyFlag::eRETAIN_ACCELERATIONS))){
            body->setRigidBodyFlag(PxRigidBodyFlag::eRETAIN_ACCELERATIONS,true);retained.push_back(body);
        }
    }
    for(int i=0;i<count;++i){step(h);scene.simulate(h);scene.fetchResults(true);}
    for(auto body:retained){body->clearForce();body->clearTorque();body->setRigidBodyFlag(PxRigidBodyFlag::eRETAIN_ACCELERATIONS,false);}
}
template<class State> void SimulateVehicle(physx::PxPhysics& physics,physx::PxScene& scene,physx::PxMaterial& material,physx::PxRigidDynamic& body,const VehicleSettings& settings,const physx::PxVec3& dimensions,State& state,float dt){
    using namespace physx;
    if(!std::isfinite(dt)||dt<=0||!dimensions.isFinite())return;
        auto s=settings;s.Sanitize();state.contacts=0;
        if(!state.dynamics)state.dynamics=std::make_shared<VehicleDynamics>();auto& d=*state.dynamics;d.Configure(s);
        if(!d.bodyConfigured){
            if(!s.assembly.empty()&&body.getNbShapes()==1){auto shape=PxRigidActorExt::createExclusiveShape(body,PxBoxGeometry(.725f,.325f,.9f),material);if(shape)shape->setLocalPose(PxTransform(PxVec3(0,.52f,-.25f)));}
            body.setCMassLocalPose(PxTransform(PxVec3(0,s.comHeight,s.comForward)));
            float mass=body.getMass(),width=PxAbs(dimensions.x),length=PxAbs(dimensions.z),height=1.1f;
            body.setMassSpaceInertiaTensor(PxVec3(mass*(height*height+length*length)/12,mass*(width*width+length*length)/12,mass*(width*width+height*height)/12));d.bodyConfigured=true;
        }
        if(!d.wheelMesh||d.meshRadius!=s.radius||d.meshWidth!=s.wheelWidth){
            if(d.wheelMesh){d.wheelMesh->release();d.wheelMesh=nullptr;}
            PxVec3 points[32];for(int i=0;i<16;++i){float angle=i*PxTwoPi/16;points[2*i]=PxVec3(-s.wheelWidth*.5f,s.radius*std::cos(angle),s.radius*std::sin(angle));points[2*i+1]=PxVec3(s.wheelWidth*.5f,points[2*i].y,points[2*i].z);}
            PxConvexMeshDesc desc;desc.points.count=32;desc.points.stride=sizeof(PxVec3);desc.points.data=points;desc.flags=PxConvexFlag::eCOMPUTE_CONVEX;
            d.wheelMesh=PxCreateConvexMesh(PxCookingParams(physics.getTolerancesScale()),desc,physics.getPhysicsInsertionCallback());d.meshRadius=s.radius;d.meshWidth=s.wheelWidth;
            if(!d.wheelMesh)std::cerr<<"Vehicle: wheel convex cooking failed; ray contact fallback active\n";
        }
        const auto pose=body.getGlobalPose();const auto comPose=pose*body.getCMassLocalPose();const auto up=pose.q.rotate(PxVec3(0,1,0));const auto forward=pose.q.rotate(PxVec3(0,0,1));
        const float mass=body.getMass();state.speed=body.getLinearVelocity().dot(forward);
        float command=state.throttle,brake=state.brake;
        if(command*state.speed<-.5f){brake=(std::max)(brake,PxAbs(command));command=0;}
        float targetAngle=state.steer*s.steerDegrees*PxPi/180.f/(1+PxAbs(state.speed)*.035f);
        d.steering+=(std::clamp)(targetAngle-d.steering,-s.steerRate*dt,s.steerRate*dt);
        d.UpdateGear(s,command,state.speed,state.manualGear,dt);
        struct Contact {bool hit=false;PxVec3 point{0},normal{0,1,0},velocity{0},heading{0,0,1},right{1,0,0};float load=0,friction=1;PxRigidDynamic* ground=nullptr;} contacts[4];
        for(int i=0;i<4;++i){
            d.steerAngles[i]=VehicleAckermann(d.steering,i,s);auto& c=contacts[i];
            auto anchor=pose.transform(PxVec3((i&1?1.f:-1.f)*s.track*.5f,0,(i<2?1.f:-1.f)*s.wheelbase*.5f));
            VehicleGroundFilter filter(&body);PxQueryFilterData query(PxQueryFlag::eSTATIC|PxQueryFlag::eDYNAMIC|PxQueryFlag::ePREFILTER);
            state.lengths[i]=s.restLength+s.travel;PxShape* shape=nullptr;
            if(d.wheelMesh){
                PxSweepBuffer hit;auto orientation=pose.q*PxQuat(d.steerAngles[i],PxVec3(0,1,0));
                auto start=anchor-up*(s.restLength-s.travel);
                if(scene.sweep(PxConvexMeshGeometry(d.wheelMesh),PxTransform(start,orientation),-up,2*s.travel,hit,PxHitFlag::eDEFAULT|PxHitFlag::eMTD,query,&filter)&&hit.hasBlock&&hit.block.normal.dot(up)>.2f){
                    c.hit=true;c.point=hit.block.position;c.normal=hit.block.normal;c.ground=hit.block.actor->is<PxRigidDynamic>();shape=hit.block.shape;
                    state.lengths[i]=s.restLength-s.travel+hit.block.distance;
                }
            }else{
                PxRaycastBuffer hit;if(scene.raycast(anchor,-up,s.restLength+s.travel+s.radius,hit,PxHitFlag::eDEFAULT,query,&filter)&&hit.hasBlock&&hit.block.normal.dot(up)>.2f){
                    c.hit=true;c.point=hit.block.position;c.normal=hit.block.normal;c.ground=hit.block.actor->is<PxRigidDynamic>();shape=hit.block.shape;state.lengths[i]=hit.block.distance-s.radius;
                }
            }
            if(!c.hit)continue;++state.contacts;
            state.lengths[i]=(std::clamp)(state.lengths[i],s.restLength-s.travel,s.restLength+s.travel);
            c.velocity=PxRigidBodyExt::getVelocityAtPos(body,c.point);if(c.ground)c.velocity-=PxRigidBodyExt::getVelocityAtPos(*c.ground,c.point);
            float compressionSpeed=-c.velocity.dot(up),damper=s.damping*(compressionSpeed>0?.65f:1.f);
            float bump=(std::max)(0.f,.025f-(state.lengths[i]-(s.restLength-s.travel)))*s.spring*4;
            c.load=(std::clamp)(s.spring*(s.restLength-state.lengths[i])+damper*compressionSpeed+bump,0.f,mass*9.81f*2.5f);
            c.heading=pose.q.rotate(PxQuat(d.steerAngles[i],PxVec3(0,1,0)).rotate(PxVec3(0,0,1)));c.heading-=c.normal*c.heading.dot(c.normal);c.heading.normalize();c.right=c.normal.cross(c.heading);c.right.normalize();
            PxMaterial* material=nullptr;if(shape)shape->getMaterials(&material,1);
            c.friction=s.grip*(1-.45f*s.wetness)*(material?(std::clamp)(material->getDynamicFriction()/.5f,.1f,2.f):1.f);
        }
        for(int axle=0;axle<2;++axle){int l=2*axle,r=l+1;if(contacts[l].hit&&contacts[r].hit){float force=(state.lengths[r]-state.lengths[l])*(axle==0?s.antiRollFront:s.antiRollRear);float bound=(std::min)(contacts[l].load,contacts[r].load);force=(std::clamp)(force,-bound,bound);contacts[l].load+=force;contacts[r].load-=force;}}
        PxVec3 accumulated[4]={PxVec3(0),PxVec3(0),PxVec3(0),PxVec3(0)},deltaVelocity(0),deltaAngular(0);
        const int steps=(std::clamp)(int(std::ceil(dt*4800)),32,128);const float h=dt/steps;
        PxVehicleTireForceParams tire{};tire.latStiffX=2;tire.latStiffY=s.lateralStiffness;tire.longStiff=s.longitudinalStiffness;tire.camberStiff=1000;tire.restLoad=mass*9.81f*.25f;
        tire.frictionVsSlip[0][0]=0;tire.frictionVsSlip[0][1]=1;tire.frictionVsSlip[1][0]=.12f;tire.frictionVsSlip[1][1]=1;tire.frictionVsSlip[2][0]=1;tire.frictionVsSlip[2][1]=.78f;
        tire.loadFilter[0][0]=tire.loadFilter[0][1]=0;tire.loadFilter[1][0]=tire.loadFilter[1][1]=4;
        for(int sub=0;sub<steps;++sub){
            float tractionScale=1;PxVec3 sum(0),torque(0);
            for(int i=0;i<4;++i){auto& c=contacts[i];auto& force=d.tireForces[i];force.setToDefault();
                float longitudinal=c.velocity.dot(c.heading);float wheelSpeed=d.wheelStates[i].rotationSpeed*s.radius;
                float b=brake*s.brakeForce*s.radius*(i<2?s.frontBrakeBias:1-s.frontBrakeBias)*.5f;
                if(s.abs&&PxAbs(longitudinal)>2&&PxAbs(wheelSpeed)<PxAbs(longitudinal)*.8f)b=0;
                b+=(state.handbrake&&i>=2)?s.brakeForce*s.radius*.5f:0;

                d.brakeTorques[i]=b;
                if(!c.hit){d.slips[i]=d.lateralSlips[i]=0;continue;}
                auto velocity=c.velocity+deltaVelocity+deltaAngular.cross(c.point-comPose.p);
                PxVehicleTireSpeedState speeds{};speeds.speedStates[PxVehicleTireDirectionModes::eLONGITUDINAL]=velocity.dot(c.heading);speeds.speedStates[PxVehicleTireDirectionModes::eLATERAL]=velocity.dot(c.right);
                PxVehicleWheelActuationState action{};action.isBrakeApplied=b>0;action.isDriveApplied=PxAbs(command)>.01f;
                PxVehicleTireSlipParams slipParams{1,4,2};PxVehicleTireSlipState slip{};
                PxVehicleTireSlipsUpdate(d.wheels[i],slipParams,action,speeds,d.wheelStates[i],slip);d.slips[i]=slip.slips[PxVehicleTireDirectionModes::eLONGITUDINAL];d.lateralSlips[i]=slip.slips[PxVehicleTireDirectionModes::eLATERAL];
                if(s.tractionControl&&d.differentialParams.torqueRatios[i]>0&&PxAbs(wheelSpeed)>PxAbs(longitudinal)+2)tractionScale=(std::min)(tractionScale,.3f);
                PxVehicleSuspensionForce suspension{};suspension.force=up*c.load;suspension.normalForce=c.load*(std::max)(0.f,c.normal.dot(up));
                PxVehicleTireGripState grip{};PxVehicleTireGripUpdate(tire,c.friction,true,suspension,slip,grip);
                grip.friction*=std::pow((std::max)(.2f,grip.load/tire.restLoad),-.12f);
                PxVehicleTireDirectionState directions{};directions.directions[PxVehicleTireDirectionModes::eLONGITUDINAL]=c.heading;directions.directions[PxVehicleTireDirectionModes::eLATERAL]=c.right;
                PxVehicleSuspensionParams suspensionParams{};suspensionParams.suspensionAttachment=PxTransform(comPose.transformInv(c.point));
                PxVehicleSuspensionComplianceState compliance;compliance.setToDefault();PxVehicleTireCamberAngleState camber{};camber.camberAngle=std::asin((std::clamp)(pose.q.rotate(PxVec3(1,0,0)).dot(c.normal),-1.f,1.f));
                PxVehicleRigidBodyState rigid{};rigid.setToDefault();rigid.pose=comPose;
                PxVehicleTireForcesUpdate(d.wheels[i],suspensionParams,tire,compliance,grip,directions,slip,camber,rigid,force);
                force.wheelTorque-=PxSign(d.wheelStates[i].rotationSpeed)*s.rollingResistance*c.load*s.radius;
                PxVec3 f=force.forces[0]+force.forces[1]+up*c.load;accumulated[i]+=f/float(steps);sum+=f;torque+=(c.point-comPose.p).cross(f);
            }
            float target=PxAbs(command)*tractionScale;if(PxAbs(state.speed)>s.maxSpeed)target=0;
            d.throttle+=(std::clamp)(target-d.throttle,-6*h,3*h);d.Drive(s,state.speed,h);
            deltaVelocity+=(sum/mass+scene.getGravity())*h;
            auto inertia=body.getMassSpaceInertiaTensor();auto local=comPose.q.rotateInv(torque);deltaAngular+=comPose.q.rotate(PxVec3(local.x/inertia.x,local.y/inertia.y,local.z/inertia.z))*h;
        }
        for(int i=0;i<4;++i)if(contacts[i].hit){PxRigidBodyExt::addForceAtPos(body,accumulated[i]*dt,contacts[i].point,PxForceMode::eIMPULSE);if(contacts[i].ground&&!(contacts[i].ground->getRigidBodyFlags()&PxRigidBodyFlag::eKINEMATIC))PxRigidBodyExt::addForceAtPos(*contacts[i].ground,-accumulated[i]*dt,contacts[i].point,PxForceMode::eIMPULSE);}
        auto velocity=body.getLinearVelocity();float speed=velocity.magnitude();body.addForce((-velocity*speed*(.6125f*s.dragArea)-up*(.6125f*s.downforceArea*speed*speed))*dt,PxForceMode::eIMPULSE);
        state.spin=d.wheelStates[0].rotationAngle;
}
}
