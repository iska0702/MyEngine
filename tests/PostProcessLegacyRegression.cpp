#include "PostProcess/PostProcessSerialization.h"
#include <iostream>
#include <sstream>

int main() {
    using namespace EnginePostProcess;
    PostProcessVolume legacy;
    legacy.name="Dark Crypt Volume";legacy.center={-15,0,-15};legacy.extents={12,8,12};legacy.blendDistance=4;legacy.priority=2;
    legacy.settings.exposure=.65f;legacy.settings.tonemapper=TonemapOperator::Reinhard;legacy.settings.contrast=1.3f;
    legacy.settings.saturation=.45f;legacy.settings.colorTemperature=-.4f;legacy.settings.tint=-.2f;
    legacy.settings.vignetteIntensity=.65f;legacy.settings.filmGrainIntensity=.1f;legacy.settings.chromaticAberration=.007f;
    std::ostringstream saved;saved.precision(9);WritePostProcessState(saved,PostProcessSettings{}, {legacy});
    std::istringstream lines(saved.str());std::string line;std::vector<PostProcessVolume> loaded;PostProcessSettings global;
    while(std::getline(lines,line)){std::istringstream row(line);std::string tag;row>>tag;ReadPostProcessState(tag,row,global,loaded);}
    if(loaded.size()!=1||!IsLegacyAutomaticCryptVolume(loaded[0]))return 1;
    loaded[0].settings.exposure=.7f;
    if(IsLegacyAutomaticCryptVolume(loaded[0]))return 2;
    loaded[0]=legacy;loaded[0].center.x=-14;
    if(IsLegacyAutomaticCryptVolume(loaded[0]))return 3;
    std::cout<<"PASS: legacy volume detected; edited settings and positions preserved\n";
    return 0;
}
