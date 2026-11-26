#pragma once
#include "VehicleSettings.h"
#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleAPI.h>
#include <cmath>
namespace EnginePhysics {
template<class T> inline physx::PxVehicleArrayData<const T> VehicleRead(const T* data){physx::PxVehicleArrayData<const T> r;r.setData(data);return r;}
template<class T> inline physx::PxVehicleArrayData<T> VehicleWrite(T* data){physx::PxVehicleArrayData<T> r;r.setData(data);return r;}
struct VehicleDynamics {
    physx::PxVehicleAxleDescription axles{};
    physx::PxVehicleEngineParams engineParams{};
    physx::PxVehicleGearboxParams gearboxParams{};
    physx::PxVehicleFourWheelDriveDifferentialParams differentialParams{};
    physx::PxVehicleClutchParams clutchParams{};
    physx::PxVehicleWheelParams wheels[4]{};
    physx::PxVehicleWheelRigidBody1dState wheelStates[4]{};
    physx::PxVehicleTireForce tireForces[4]{};
    physx::PxVehicleWheelActuationState actuation[4]{};
    physx::PxVehicleEngineState engine{};
    physx::PxVehicleGearboxState gearbox{};
    physx::PxVehicleDifferentialState differential{};
    physx::PxVehicleWheelConstraintGroupState groups{};
    physx::PxVehicleClutchSlipState clutchSlip{};
    physx::PxVehicleEngineDriveThrottleCommandResponseState throttleResponse{};
    physx::PxVehicleClutchCommandResponseState clutchResponse{};
    float brakeTorques[4]{},steerAngles[4]{},slips[4]{},lateralSlips[4]{};
    float steering=0,throttle=0,shiftCooldown=0,meshRadius=0,meshWidth=0,clutchCoupling=0;
    physx::PxConvexMesh* wheelMesh=nullptr;
    bool bodyConfigured=false;
    VehicleDynamics(){
        physx::PxU32 front[2]={0,1},rear[2]={2,3};axles.setToDefault();axles.addAxle(2,front);axles.addAxle(2,rear);
        gearbox.setToDefault();gearbox.currentGear=gearbox.targetGear=2;engine.rotationSpeed=900*physx::PxPi/30;
        for(int i=0;i<4;++i){wheelStates[i].setToDefault();tireForces[i].setToDefault();}
    }
    ~VehicleDynamics(){if(wheelMesh)wheelMesh->release();}
    VehicleDynamics(const VehicleDynamics&)=delete;
    VehicleDynamics& operator=(const VehicleDynamics&)=delete;
    void Configure(const VehicleSettings& s){
        using namespace physx;
        engineParams={};engineParams.peakTorque=s.peakTorque;engineParams.moi=s.engineInertia;
        engineParams.idleOmega=s.idleRPM*PxPi/30;engineParams.maxOmega=s.redlineRPM*PxPi/30;
        engineParams.dampingRateFullThrottle=.15f;engineParams.dampingRateZeroThrottleClutchEngaged=.3f;engineParams.dampingRateZeroThrottleClutchDisengaged=.35f;
        engineParams.torqueCurve.addPair(0,.65f);engineParams.torqueCurve.addPair(.3f,.9f);engineParams.torqueCurve.addPair(.6f,1);engineParams.torqueCurve.addPair(.85f,.9f);engineParams.torqueCurve.addPair(1,.65f);
        gearboxParams={};gearboxParams.neutralGear=1;gearboxParams.nbRatios=8;gearboxParams.finalRatio=s.finalDrive;gearboxParams.switchTime=s.shiftTime;gearboxParams.ratios[0]=-s.reverseRatio;
        for(int i=0;i<6;++i)gearboxParams.ratios[i+2]=s.gears[i];
        clutchParams.accuracyMode=PxVehicleClutchAccuracyMode::eBEST_POSSIBLE;clutchParams.estimateIterations=8;
        differentialParams={};differentialParams.frontWheelIds[0]=0;differentialParams.frontWheelIds[1]=1;differentialParams.rearWheelIds[0]=2;differentialParams.rearWheelIds[1]=3;
        differentialParams.frontBias=s.frontDrive>0?s.differentialBias:0;differentialParams.rearBias=s.frontDrive<1?s.differentialBias:0;
        differentialParams.centerBias=s.frontDrive>0&&s.frontDrive<1?s.differentialBias:0;
        differentialParams.frontTarget=differentialParams.rearTarget=differentialParams.centerTarget=(std::min)(1.5f,1.f+(s.differentialBias-1.f)*.5f);differentialParams.rate=5;
        for(int i=0;i<4;++i){
            wheels[i].radius=s.radius;wheels[i].halfWidth=s.wheelWidth*.5f;wheels[i].mass=s.wheelMass;wheels[i].moi=.5f*s.wheelMass*s.radius*s.radius;wheels[i].dampingRate=.25f;
            float fraction=(i<2?s.frontDrive:1-s.frontDrive)*.5f;differentialParams.torqueRatios[i]=fraction;differentialParams.aveWheelSpeedRatios[i]=fraction;
        }
    }
    void UpdateGear(const VehicleSettings& s,float command,float speed,int manualGear,float dt){
        using namespace physx;
        shiftCooldown=(std::max)(0.f,shiftCooldown-dt);
        PxU32 target=gearbox.targetGear;
        if(!s.automaticGears)target=PxU32((std::clamp)(manualGear,-1,6)+1);
        else{
            if(command<-.05f&&PxAbs(speed)<.5f)target=0;
            else if(command>.05f&&PxAbs(speed)<.5f&&target<2)target=2;
            float rpm=engine.rotationSpeed*30/PxPi;
            if(target>=2&&shiftCooldown<=0&&gearbox.currentGear==gearbox.targetGear){
                if(rpm>s.redlineRPM*.82f&&target<7){++target;shiftCooldown=1;}
                else if(rpm<s.redlineRPM*.34f&&target>2){--target;shiftCooldown=1;}
            }
        }
        PxVehicleGearCommandResponseUpdate(target,gearboxParams,gearbox);PxVehicleGearboxUpdate(gearboxParams,dt,gearbox);
    }
    void Drive(const VehicleSettings& s,float speed,float dt){
        using namespace physx;
        auto wheelRead=VehicleRead(wheelStates);
        PxVehicleDifferentialStateUpdate(axles,differentialParams,wheelRead,dt,differential,groups);
        throttleResponse.commandResponse=throttle;
        float launch=(std::clamp)((engine.rotationSpeed-engineParams.idleOmega-throttle*500*PxPi/30)/(1000*PxPi/30),0.f,1.f);
        float target=(throttle<.02f&&PxAbs(speed)<.5f)?0.f:(std::max)(launch,(std::min)(1.f,PxAbs(speed)/3));
        if(gearbox.currentGear==1)target=0;
        clutchCoupling+=(std::clamp)(target-clutchCoupling,-6*dt,3*dt);
        clutchResponse.normalisedCommandResponse=clutchCoupling;
        clutchResponse.commandResponse=clutchResponse.normalisedCommandResponse*s.clutchStrength;
        for(int i=0;i<4;++i){actuation[i].isBrakeApplied=brakeTorques[i]>0;actuation[i].isDriveApplied=throttle>0&&differentialParams.torqueRatios[i]>0&&gearbox.currentGear!=1;}
        auto mutableWheels=VehicleWrite(wheelStates);
        PxVehicleEngineDrivetrainUpdate(axles,VehicleRead(wheels),engineParams,clutchParams,gearboxParams,VehicleRead(brakeTorques),VehicleRead(actuation),VehicleRead(tireForces),gearbox,throttleResponse,clutchResponse,differential,&groups,dt,mutableWheels,engine,clutchSlip);
        for(int i=0;i<4;++i){wheelStates[i].rotationAngle=std::remainder(wheelStates[i].rotationAngle+wheelStates[i].rotationSpeed*dt,PxTwoPi);}
    }
};
inline float VehicleAckermann(float angle,int wheel,const VehicleSettings& s){
    if(wheel>=2||std::abs(angle)<.0001f)return 0;
    float radius=s.wheelbase/std::tan(std::abs(angle));bool inside=(angle>0)==(wheel==1);
    float corrected=std::atan(s.wheelbase/(std::max)(.1f,radius+(inside?-1:1)*s.track*.5f));
    return angle*(1-s.ackermann)+std::copysign(corrected,angle)*s.ackermann;
}
}
