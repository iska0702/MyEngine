#pragma once
#include "CameraSystem.h"
#include <sstream>
#include <iomanip>
inline std::string WriteCameraDocument(){
    auto& system=CameraSystem::Get();std::ostringstream out;out.precision(9);out<<"CAMERA_ACTIVE "<<system.GetActiveIndex()<<'\n';
    for(size_t i=0;i<system.GetCameraCount();++i){const auto& c=system.GetCameraProfile(i);
        out<<"CAMERA_PROFILE "<<std::quoted(c.name)<<' '<<int(c.mode)<<' '<<c.position.x<<' '<<c.position.y<<' '<<c.position.z<<' '<<c.pitch<<' '<<c.yaw<<' '<<c.roll<<' '<<c.fov<<' '<<c.nearPlane<<' '<<c.farPlane<<' '<<c.flySpeed<<' '
           <<c.isOscillating<<' '<<c.baseYaw<<' '<<c.sweepAngle<<' '<<c.sweepSpeed<<' '<<c.orbitTarget.x<<' '<<c.orbitTarget.y<<' '<<c.orbitTarget.z<<' '<<c.orbitRadius<<' '<<c.orbitSpeed<<' '<<c.orbitElevation<<' '
           <<c.lookAtTarget.x<<' '<<c.lookAtTarget.y<<' '<<c.lookAtTarget.z<<' '<<c.showGizmoInWorld<<'\n';
    }return out.str();
}
inline void ReadCameraDocument(const std::string& text){
    std::istringstream all(text);std::string line;std::vector<CameraProfile> profiles;size_t active=0;
    while(std::getline(all,line)){
        std::istringstream in(line);std::string tag;in>>tag;
        if(tag=="CAMERA_ACTIVE")in>>active;
        if(tag!="CAMERA_PROFILE")continue;CameraProfile c;int mode;
        if(in>>std::quoted(c.name)>>mode>>c.position.x>>c.position.y>>c.position.z>>c.pitch>>c.yaw>>c.roll>>c.fov>>c.nearPlane>>c.farPlane>>c.flySpeed
           >>c.isOscillating>>c.baseYaw>>c.sweepAngle>>c.sweepSpeed>>c.orbitTarget.x>>c.orbitTarget.y>>c.orbitTarget.z>>c.orbitRadius>>c.orbitSpeed>>c.orbitElevation
           >>c.lookAtTarget.x>>c.lookAtTarget.y>>c.lookAtTarget.z>>c.showGizmoInWorld){
            if(mode<0||mode>5||profiles.size()>=256)continue;c.mode=static_cast<CameraMode>(mode);profiles.push_back(c);
        }
    }
    if(!profiles.empty())CameraSystem::Get().ReplaceProfiles(profiles,active);
}
