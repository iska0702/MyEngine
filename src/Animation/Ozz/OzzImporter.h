#pragma once
#include "OzzRuntime.h"
#include <string>
#include <vector>

namespace EngineAnim {

class OzzImporter {
public:
    static bool ExtractSkeletonFromFBX(const std::string& fbxPath, OzzSkeleton& outSkeleton);
    static bool ExtractAnimationFromFBX(const std::string& fbxPath, const OzzSkeleton& skeleton, OzzAnimation& outAnimation, float sampleRate = 60.0f);

    static bool ConvertFBXToOzzSkeleton(const std::string& fbxPath, const std::string& outputOzzPath, OzzSkeleton& outSkeleton);
    static bool ConvertFBXToOzzAnimation(const std::string& fbxPath, const OzzSkeleton& skeleton, const std::string& outputOzzPath, OzzAnimation& outAnimation, float sampleRate = 60.0f);
};

} // namespace EngineAnim
