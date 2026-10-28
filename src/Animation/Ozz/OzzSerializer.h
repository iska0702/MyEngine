#pragma once
#include "OzzRuntime.h"
#include <string>

namespace EngineAnim {

class OzzSerializer {
public:
    static bool SaveSkeleton(const OzzSkeleton& skeleton, const std::string& filepath);
    static bool LoadSkeleton(OzzSkeleton& skeleton, const std::string& filepath);

    static bool SaveAnimation(const OzzAnimation& animation, const std::string& filepath);
    static bool LoadAnimation(OzzAnimation& animation, const std::string& filepath);
};

} // namespace EngineAnim
