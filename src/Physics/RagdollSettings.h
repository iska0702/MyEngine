#pragma once
#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <string>
namespace EnginePhysics {
struct RagdollSettings {
    bool useProjectDefaults=true,gravity=true,ccd=true;
    float mass=70,radius=.08f,angularDamping=.5f,linearDamping=.1f,jointLimit=45;
    void Sanitize(){
        auto clamp=[](float& value,float lo,float hi,float fallback){value=std::isfinite(value)?(std::clamp)(value,lo,hi):fallback;};
        clamp(mass,1,1000,70);clamp(radius,.01f,.5f,.08f);clamp(angularDamping,0,20,.5f);clamp(linearDamping,0,20,.1f);clamp(jointLimit,5,120,45);
    }
};
inline void WriteRagdoll(std::ostream& out,const RagdollSettings& s){out<<"RAGDOLL_SETTINGS "<<s.useProjectDefaults<<' '<<s.mass<<' '<<s.radius<<' '<<s.angularDamping<<' '<<s.linearDamping<<' '<<s.jointLimit<<' '<<s.gravity<<' '<<s.ccd<<'\n';}
inline bool ReadRagdoll(const std::string& tag,std::istream& in,RagdollSettings& s){
    if(tag!="RAGDOLL_SETTINGS")return false;
    RagdollSettings candidate;if(in>>candidate.useProjectDefaults>>candidate.mass>>candidate.radius>>candidate.angularDamping>>candidate.linearDamping>>candidate.jointLimit>>candidate.gravity>>candidate.ccd){candidate.Sanitize();s=candidate;}return true;
}
}
