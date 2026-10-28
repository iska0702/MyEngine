#pragma once
#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
namespace EngineRender {
struct TessellationSettings {
    bool enabled=false;
    float factor=4, smoothing=.5f, nearDistance=5, farDistance=50;
    void Normalize(){
        auto finite=[](float v,float fallback){return std::isfinite(v)?v:fallback;};
        factor=std::clamp(finite(factor,4),1.f,16.f);
        smoothing=std::clamp(finite(smoothing,.5f),0.f,1.f);
        nearDistance=std::clamp(finite(nearDistance,5),0.f,9999.f);
        farDistance=std::clamp(finite(farDistance,50),nearDistance+.1f,10000.f);
    }
};
inline void WriteTessellation(std::ostream& out,const TessellationSettings& t){out<<t.enabled<<' '<<t.factor<<' '<<t.smoothing<<' '<<t.nearDistance<<' '<<t.farDistance;}
inline bool ReadTessellation(std::istream& in,TessellationSettings& target){
    TessellationSettings t;
    if(!(in>>t.enabled>>t.factor>>t.smoothing>>t.nearDistance>>t.farDistance))return false;
    for(float v:{t.factor,t.smoothing,t.nearDistance,t.farDistance})if(!std::isfinite(v))return false;
    t.Normalize();target=t;return true;
}
}
