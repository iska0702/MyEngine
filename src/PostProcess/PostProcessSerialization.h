#pragma once
#include "PostProcessTypes.h"
#include <ostream>
#include <istream>
#include <iomanip>

namespace EnginePostProcess {
inline void WritePostSettings(std::ostream& out,const PostProcessSettings& s) {
    out<<static_cast<int>(s.tonemapper)<<' '<<s.bloomEnabled<<' '<<s.dofEnabled;
    for(const float* field : {&s.exposure,&s.bloomIntensity,&s.bloomThreshold,&s.bloomScatter,&s.contrast,&s.saturation,
        &s.colorFilter.x,&s.colorFilter.y,&s.colorFilter.z,&s.colorTemperature,&s.tint,
        &s.shadowTint.x,&s.shadowTint.y,&s.shadowTint.z,&s.highlightTint.x,&s.highlightTint.y,&s.highlightTint.z,
        &s.chromaticAberration,&s.vignetteIntensity,&s.vignetteSmoothness,&s.filmGrainIntensity,&s.lensDistortion,
        &s.focusDistance,&s.focalLength,&s.aperture,&s.maxBlurRadius}) out<<' '<<*field;
    out<<'\n';
}
inline bool ReadPostSettings(std::istream& in,PostProcessSettings& destination) {
    PostProcessSettings s;int tone;
    if(!(in>>tone>>s.bloomEnabled>>s.dofEnabled))return false;
    for(float* field : {&s.exposure,&s.bloomIntensity,&s.bloomThreshold,&s.bloomScatter,&s.contrast,&s.saturation,
        &s.colorFilter.x,&s.colorFilter.y,&s.colorFilter.z,&s.colorTemperature,&s.tint,
        &s.shadowTint.x,&s.shadowTint.y,&s.shadowTint.z,&s.highlightTint.x,&s.highlightTint.y,&s.highlightTint.z,
        &s.chromaticAberration,&s.vignetteIntensity,&s.vignetteSmoothness,&s.filmGrainIntensity,&s.lensDistortion,
        &s.focusDistance,&s.focalLength,&s.aperture,&s.maxBlurRadius})
        if(!(in>>*field) || !std::isfinite(*field))return false;
    if(tone<0 || tone>4)return false;
    s.tonemapper=static_cast<TonemapOperator>(tone);destination=s;return true;
}
inline void WritePostProcessState(std::ostream& out,const PostProcessSettings& global,const std::vector<PostProcessVolume>& volumes) {
    out<<"POST_GLOBAL ";WritePostSettings(out,global);
    for(const auto& v:volumes) {
        out<<"POST_VOLUME "<<std::quoted(v.name)<<' '<<v.isGlobal<<' '<<v.center.x<<' '<<v.center.y<<' '<<v.center.z<<' '
            <<v.extents.x<<' '<<v.extents.y<<' '<<v.extents.z<<' '<<v.blendDistance<<' '<<v.priority<<' ';
        WritePostSettings(out,v.settings);
    }
}
inline bool ReadPostProcessState(const std::string& tag,std::istream& in,PostProcessSettings& global,std::vector<PostProcessVolume>& volumes) {
    if(tag=="POST_GLOBAL") { ReadPostSettings(in,global);return true; }
    if(tag!="POST_VOLUME")return false;
    PostProcessVolume v;
    if(in>>std::quoted(v.name)>>v.isGlobal>>v.center.x>>v.center.y>>v.center.z>>v.extents.x>>v.extents.y>>v.extents.z>>v.blendDistance>>v.priority)
        if(ReadPostSettings(in,v.settings) && volumes.size()<256)volumes.push_back(v);
    return true;
}
}
