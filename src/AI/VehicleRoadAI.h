#pragma once
#include "Physics/VehicleSettings.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace EngineAI {
struct VehicleRoadState { size_t waypoint=0; float stoppedFor=0; bool complete=false; };
struct VehicleRoadControl { float throttle=0, steer=0, brake=1; };
inline VehicleRoadControl DriveRoad(const EnginePhysics::VehicleSettings& settings, VehicleRoadState& state,
                                    float x,float z,float yaw,float speed,float obstacleDistance,float dt) {
    VehicleRoadControl out;
    if (!settings.aiEnabled || settings.route.empty() || state.complete || !std::isfinite(dt) || dt<=0 ||
        !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(yaw) || !std::isfinite(speed)) return out;
    if (state.waypoint>=settings.route.size()) state.waypoint=0;
    const auto* point=&settings.route[state.waypoint];
    float dx=point->x-x,dz=point->z-z,dist=std::sqrt(dx*dx+dz*dz);
    if (dist < 2.0f) {
        if(point->redSignal) { state.stoppedFor=0; return out; }
        if(point->stop) {
            if(std::abs(speed)>0.5f) { state.stoppedFor=0; return out; }
            state.stoppedFor+=dt;
            if(state.stoppedFor<2.0f) return out;
        }
        state.stoppedFor=0;
        if(++state.waypoint>=settings.route.size()) {
            if(!settings.aiLoop) { state.complete=true;return out; }
            state.waypoint=0;
        }
        point=&settings.route[state.waypoint]; dx=point->x-x;dz=point->z-z;dist=std::sqrt(dx*dx+dz*dz);
    }
    float error=std::atan2(dx,dz)-yaw;
    while(error>3.14159265f)error-=6.2831853f;
    while(error<-3.14159265f)error+=6.2831853f;
    out.steer=(std::clamp)(error*1.4f,-1.f,1.f);
    float target=(std::min)(settings.maxSpeed,point->speedLimit);
    target *= (std::max)(0.25f,1.f-std::abs(error)*0.55f);
    if(point->stop||point->redSignal) target=(std::min)(target,std::sqrt(2.f*3.f*(std::max)(0.f,dist-1.2f)));
    if(std::isfinite(obstacleDistance)) target=(std::min)(target,std::sqrt(2.f*4.f*(std::max)(0.f,obstacleDistance-3.f)));
    if(speed< -0.5f) {out.brake=1;return out;}
    const float delta=target-speed;
    out.throttle=(std::clamp)(delta*0.4f,0.f,1.f);
    out.brake=(std::clamp)(-delta*0.45f,0.f,1.f);
    if(target<0.2f){out.throttle=0;out.brake=1;}
    return out;
}
}
