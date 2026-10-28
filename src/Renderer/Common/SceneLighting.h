#pragma once
#include "RenderTypes.h"
#include <iomanip>
#include <ostream>
#include <istream>
#include <algorithm>
#include <iterator>
#include <cmath>

namespace EngineRender {
inline void UploadSceneLights(SceneConstantBuffer& buffer,const std::vector<PointLight>& points,const std::vector<SpotLight>& spots){
    buffer.numPointLights=0;buffer.numSpotLights=0;
    std::fill(std::begin(buffer.pointLights),std::end(buffer.pointLights),PointLightData{});
    std::fill(std::begin(buffer.spotLights),std::end(buffer.spotLights),SpotLightData{});
    for(auto& light:buffer.pointLights)light.intensity=0;
    for(auto& light:buffer.spotLights){light.intensity=0;light.enabled=0;}
    for(const auto& light:points){
        if(!light.enabled||light.intensity<=0||light.range<=.01f)continue;
        if(buffer.numPointLights==MAX_POINT_LIGHTS)break;
        buffer.pointLights[buffer.numPointLights++]=light.ToShaderData();
    }
    for(auto light:spots){
        if(!light.enabled||light.intensity<=0||light.range<=.01f)continue;
        if(buffer.numSpotLights==MAX_SPOT_LIGHTS)break;
        float length=light.direction.x*light.direction.x+light.direction.y*light.direction.y+light.direction.z*light.direction.z;
        if(!std::isfinite(length)||length<.000001f)light.direction={0,-1,0};
        buffer.spotLights[buffer.numSpotLights++]=light.ToShaderData();
    }
}
inline void WriteSceneLights(std::ostream& out, const std::vector<PointLight>& points, const std::vector<SpotLight>& spots) {
    for (const auto& p : points) out << "POINT_LIGHT " << std::quoted(p.name) << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
        << p.color.x << ' ' << p.color.y << ' ' << p.color.z << ' ' << p.intensity << ' ' << p.range << ' ' << p.enabled << '\n';
    for (const auto& p : spots) out << "SPOT_LIGHT " << std::quoted(p.name) << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
        << p.direction.x << ' ' << p.direction.y << ' ' << p.direction.z << ' ' << p.color.x << ' ' << p.color.y << ' ' << p.color.z << ' '
        << p.intensity << ' ' << p.range << ' ' << p.innerAngle << ' ' << p.outerAngle << ' ' << p.enabled << '\n';
}
inline bool ReadSceneLight(const std::string& tag, std::istream& in, std::vector<PointLight>& points, std::vector<SpotLight>& spots) {
    if (tag == "POINT_LIGHT") {
        PointLight p;
        if (in >> std::quoted(p.name) >> p.position.x >> p.position.y >> p.position.z >> p.color.x >> p.color.y >> p.color.z >> p.intensity >> p.range >> p.enabled)
            if (points.size() < MAX_POINT_LIGHTS) points.push_back(p);
        return true;
    }
    if (tag == "SPOT_LIGHT") {
        SpotLight p;
        if (in >> std::quoted(p.name) >> p.position.x >> p.position.y >> p.position.z >> p.direction.x >> p.direction.y >> p.direction.z
            >> p.color.x >> p.color.y >> p.color.z >> p.intensity >> p.range >> p.innerAngle >> p.outerAngle >> p.enabled)
            if (spots.size() < MAX_SPOT_LIGHTS) spots.push_back(p);
        return true;
    }
    return false;
}
}
