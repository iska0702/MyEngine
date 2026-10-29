#pragma once
#include "PostProcessTypes.h"
#include <ostream>
#include <istream>
#include <iomanip>
#include <sstream>

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
inline bool IsLegacyAutomaticCryptVolume(const PostProcessVolume& v) {
    if(v.name!="Dark Crypt Volume" || v.isGlobal || v.center.x!=-15.f || v.center.y!=0.f || v.center.z!=-15.f ||
       v.extents.x!=12.f || v.extents.y!=8.f || v.extents.z!=12.f || v.blendDistance!=4.f || v.priority!=2.f) return false;
    PostProcessSettings expected;
    expected.exposure=.65f;expected.tonemapper=TonemapOperator::Reinhard;expected.contrast=1.3f;
    expected.saturation=.45f;expected.colorTemperature=-.4f;expected.tint=-.2f;
    expected.vignetteIntensity=.65f;expected.filmGrainIntensity=.10f;expected.chromaticAberration=.007f;
    std::ostringstream actualText,expectedText;
    actualText.precision(9);expectedText.precision(9);
    WritePostSettings(actualText,v.settings);WritePostSettings(expectedText,expected);
    return actualText.str()==expectedText.str();
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
