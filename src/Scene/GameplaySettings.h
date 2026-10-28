#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
namespace EngineScene {
struct GameplaySettings {
    bool trigger=false,door=false,button=false,pickup=false,audio=false;
    float radius=2,doorAngle=90,doorSpeed=90,volume=1,cooldown=1;
    bool once=true;
    std::string target,sound;
};
inline void WriteGameplay(std::ostream& out,const GameplaySettings& s){
    out<<"GAMEPLAY "<<s.trigger<<' '<<s.door<<' '<<s.button<<' '<<s.pickup<<' '<<s.audio<<' '
       <<s.radius<<' '<<s.doorAngle<<' '<<s.doorSpeed<<' '<<s.volume<<' '<<s.cooldown<<' '
       <<s.once<<' '<<std::quoted(s.target)<<' '<<std::quoted(s.sound)<<'\n';
}
inline bool ReadGameplay(std::istream& in,GameplaySettings& settings){
    GameplaySettings s;
    if(!(in>>s.trigger>>s.door>>s.button>>s.pickup>>s.audio>>s.radius>>s.doorAngle>>s.doorSpeed>>s.volume>>s.cooldown>>s.once>>std::quoted(s.target)>>std::quoted(s.sound)))return false;
    if(!std::isfinite(s.radius)||!std::isfinite(s.doorAngle)||!std::isfinite(s.doorSpeed)||!std::isfinite(s.volume)||!std::isfinite(s.cooldown))return false;
    s.radius=std::clamp(s.radius,.1f,1000.f);s.doorAngle=std::clamp(s.doorAngle,-360.f,360.f);
    s.doorSpeed=std::clamp(s.doorSpeed,1.f,720.f);s.volume=std::clamp(s.volume,0.f,1.f);s.cooldown=std::clamp(s.cooldown,.05f,3600.f);
    settings=std::move(s);return true;
}
}
