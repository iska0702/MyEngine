#pragma once
#include <cmath>
#include <stdexcept>
#include <string>
namespace EngineProject {
struct ProjectTuning {
    float gravityX=0,gravityY=-9.81f,gravityZ=0,physicsHz=60,maxSubSteps=4;
    float walkSpeed=6,runSpeed=11,crouchSpeed=3,jumpSpeed=8.5f,pushForce=35,playerGravity=-20;
    float ragdollMass=70,ragdollRadius=.08f,ragdollDamping=.5f,ragdollLimit=45;
    float fov=75;
    template<class F> void Visit(F f){
        f("gravityX",gravityX,-100.f,100.f,"Physics","Gravity X (m/s2)");
        f("gravityY",gravityY,-100.f,100.f,"Physics","Gravity Y (m/s2)");
        f("gravityZ",gravityZ,-100.f,100.f,"Physics","Gravity Z (m/s2)");
        f("physicsHz",physicsHz,15.f,240.f,"Physics","Simulation frequency (Hz)");
        f("maxSubSteps",maxSubSteps,1.f,16.f,"Physics","Maximum substeps");
        f("walkSpeed",walkSpeed,.1f,100.f,"Player","Walk speed (m/s)");
        f("runSpeed",runSpeed,.1f,150.f,"Player","Run speed (m/s)");
        f("crouchSpeed",crouchSpeed,.1f,100.f,"Player","Crouch speed (m/s)");
        f("jumpSpeed",jumpSpeed,0.f,50.f,"Player","Jump velocity (m/s)");
        f("pushForce",pushForce,0.f,10000.f,"Player","Push force (N)");
        f("playerGravity",playerGravity,-100.f,0.f,"Player","Player gravity (m/s2)");
        f("ragdollMass",ragdollMass,1.f,1000.f,"Ragdoll","Total mass (kg)");
        f("ragdollRadius",ragdollRadius,.01f,.5f,"Ragdoll","Bone collider radius (m)");
        f("ragdollDamping",ragdollDamping,0.f,20.f,"Ragdoll","Angular damping");
        f("ragdollLimit",ragdollLimit,5.f,120.f,"Ragdoll","Joint swing limit (degrees)");
        f("fov",fov,30.f,120.f,"Camera","Field of view (degrees)");
    }
    void Validate() const {auto copy=*this;copy.Visit([](auto key,float value,float lo,float hi,auto,auto){if(!std::isfinite(value)||value<lo||value>hi)throw std::runtime_error(std::string("Invalid project setting: ")+key);});}
};
}
