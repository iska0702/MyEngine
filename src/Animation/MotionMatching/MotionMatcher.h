#pragma once
#include "MotionDatabase.h"
#include "MotionTrajectory.h"
#include <vector>

namespace EngineAnim {

struct MotionMatchResult {
    int frameIndex = -1;
    int animIndex = -1;
    float time = 0.0f;
    float cost = 1e9f;
};

class MotionMatcher {
public:
    MotionMatcher() = default;

    static MotionMatchResult Search(
        const MotionDatabase& db,
        const float* normalizedQuery,
        int currentFrameIndex,
        float transitionCost = 0.5f,
        int ignoreSurrounding = 15
    );

    // Inertialized Blending between frames
    static void InertializeTransition(
        std::vector<Transform>& inOutOffsets,
        std::vector<Transform>& inOutOffsetVels,
        const std::vector<Transform>& srcPose,
        const std::vector<Transform>& dstPose
    );

    static void InertializeUpdate(
        std::vector<Transform>& inOutPose,
        std::vector<Transform>& inOutOffsets,
        std::vector<Transform>& inOutOffsetVels,
        const std::vector<Transform>& inputPose,
        float halflife,
        float dt
    );
};

} // namespace EngineAnim
