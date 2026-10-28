#pragma once
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>
namespace EnginePhysics {
struct VehicleSettings {
    bool enabled=false;
    std::string assembly;
    int part=0; // 0 chassis, 1 cabin, 2..5 wheels, 6..9 lamps, 10..13 wheel spokes
    float wheelbase=2.7f,track=1.8f,radius=.36f,restLength=.45f,travel=.22f;
    float spring=32000,damping=4500,grip=1.2f,engineForce=6500,brakeForce=14000,maxSpeed=35,steerDegrees=32;
    float peakTorque=320,idleRPM=900,redlineRPM=6800,engineInertia=.35f,clutchStrength=10;
    float finalDrive=3.8f,shiftTime=.28f,gears[6]={3.5f,2.1f,1.5f,1.15f,.92f,.75f},reverseRatio=3.2f;
    float frontDrive=.5f,differentialBias=2.5f,wheelMass=22,wheelWidth=.28f;
    float frontBrakeBias=.62f,antiRollFront=6000,antiRollRear=4500,comHeight=-.15f,comForward=.1f;
    float longitudinalStiffness=18000,lateralStiffness=65000,wetness=0,rollingResistance=.015f,dragArea=.65f,downforceArea=.15f;
    float ackermann=1,steerRate=2.5f;
    bool automaticGears=true,abs=true,tractionControl=true,weatherGrip=true;
    void Sanitize(){
        auto clamp=[](float& v,float lo,float hi,float fallback){v=std::isfinite(v)?(std::clamp)(v,lo,hi):fallback;};
        clamp(wheelbase,.5f,10,2.7f);clamp(track,.5f,6,1.8f);clamp(radius,.1f,2,.36f);
        clamp(restLength,.1f,2,.45f);clamp(travel,.02f,.8f,.22f);travel=(std::min)(travel,restLength*.9f);
        clamp(spring,1000,200000,32000);clamp(damping,100,20000,4500);clamp(grip,.1f,3,1.2f);
        clamp(engineForce,0,50000,6500);clamp(brakeForce,0,100000,14000);clamp(maxSpeed,1,100,35);clamp(steerDegrees,1,60,32);
        clamp(peakTorque,20,2000,320);clamp(idleRPM,400,2000,900);clamp(redlineRPM,3000,12000,6800);
        clamp(engineInertia,.05f,3,.35f);clamp(clutchStrength,1,100,10);clamp(finalDrive,1,8,3.8f);clamp(shiftTime,.05f,2,.28f);
        for(float& ratio:gears)clamp(ratio,.2f,8,1);for(int i=1;i<6;++i)gears[i]=(std::min)(gears[i],gears[i-1]);
        clamp(reverseRatio,1,6,3.2f);clamp(frontDrive,0,1,.5f);clamp(differentialBias,1.1f,8,2.5f);
        clamp(wheelMass,5,100,22);clamp(wheelWidth,.1f,.8f,.28f);clamp(frontBrakeBias,.3f,.8f,.62f);
        clamp(antiRollFront,0,50000,6000);clamp(antiRollRear,0,50000,4500);clamp(comHeight,-.5f,.5f,-.15f);clamp(comForward,-.5f,.5f,.1f);
        clamp(longitudinalStiffness,1000,100000,18000);clamp(lateralStiffness,5000,150000,65000);clamp(wetness,0,1,0);
        clamp(rollingResistance,0,.1f,.015f);clamp(dragArea,.1f,3,.65f);clamp(downforceArea,0,5,.15f);clamp(ackermann,0,1,1);clamp(steerRate,.2f,10,2.5f);
        part=(std::clamp)(part,0,13);if(assembly.size()>128)assembly.clear();
    }
};
inline void WriteVehicle(std::ostream& out,const VehicleSettings& s){
    out<<"VEHICLE "<<s.enabled<<' '<<std::quoted(s.assembly)<<' '<<s.part<<' '<<s.wheelbase<<' '<<s.track<<' '<<s.radius<<' '
       <<s.restLength<<' '<<s.travel<<' '<<s.spring<<' '<<s.damping<<' '<<s.grip<<' '<<s.engineForce<<' '<<s.brakeForce<<' '<<s.maxSpeed<<' '<<s.steerDegrees<<'\n';
    out<<"VEHICLE_DYNAMICS 2 "<<s.peakTorque<<' '<<s.idleRPM<<' '<<s.redlineRPM<<' '<<s.engineInertia<<' '<<s.clutchStrength<<' '<<s.finalDrive<<' '<<s.shiftTime<<' ';
    for(float ratio:s.gears)out<<ratio<<' ';
    out<<s.reverseRatio<<' '<<s.frontDrive<<' '<<s.differentialBias<<' '<<s.wheelMass<<' '<<s.wheelWidth<<' '<<s.frontBrakeBias<<' '
       <<s.antiRollFront<<' '<<s.antiRollRear<<' '<<s.comHeight<<' '<<s.comForward<<' '<<s.longitudinalStiffness<<' '<<s.lateralStiffness<<' '
       <<s.wetness<<' '<<s.rollingResistance<<' '<<s.dragArea<<' '<<s.downforceArea<<' '<<s.ackermann<<' '<<s.steerRate<<' '<<s.automaticGears<<' '<<s.abs<<' '<<s.tractionControl<<' '<<s.weatherGrip<<'\n';
}
inline bool ReadVehicle(const std::string& tag,std::istream& in,VehicleSettings& s){
    if(tag=="VEHICLE_DYNAMICS"){
        auto v=s;int version=0;
        if(!(in>>version)||(version!=1&&version!=2))throw std::runtime_error("Unsupported vehicle dynamics version");
        in>>v.peakTorque>>v.idleRPM>>v.redlineRPM>>v.engineInertia>>v.clutchStrength>>v.finalDrive>>v.shiftTime;
        for(float& ratio:v.gears)in>>ratio;
        if(!(in>>v.reverseRatio>>v.frontDrive>>v.differentialBias>>v.wheelMass>>v.wheelWidth>>v.frontBrakeBias>>v.antiRollFront>>v.antiRollRear>>v.comHeight>>v.comForward
            >>v.longitudinalStiffness>>v.lateralStiffness>>v.wetness>>v.rollingResistance>>v.dragArea>>v.downforceArea>>v.ackermann>>v.steerRate>>v.automaticGears>>v.abs>>v.tractionControl))throw std::runtime_error("Invalid vehicle dynamics");
        if(version>=2&&!(in>>v.weatherGrip))throw std::runtime_error("Invalid weather grip setting");
        v.Sanitize();s=v;return true;
    }
    if(tag!="VEHICLE")return false;
    VehicleSettings v;
    if(!(in>>v.enabled>>std::quoted(v.assembly)>>v.part>>v.wheelbase>>v.track>>v.radius>>v.restLength>>v.travel>>v.spring>>v.damping>>v.grip>>v.engineForce>>v.brakeForce>>v.maxSpeed>>v.steerDegrees))throw std::runtime_error("Invalid vehicle settings");
    v.Sanitize();s=v;return true;
}
}
