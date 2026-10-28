#pragma once
#include "MotionFeature.h"
#include "../Ozz/OzzRuntime.h"
#include <vector>
#include <string>

namespace EngineAnim {

struct MotionWeights {
    float footPosition = 0.75f;
    float footVelocity = 1.0f;
    float hipVelocity = 1.0f;
    float trajectoryPosition = 1.5f;
    float trajectoryDirection = 1.5f;
    float continuityCost = 1.0f;
};

class MotionDatabase {
public:
    enum {
        BOUND_SM_SIZE = 16,
        BOUND_LR_SIZE = 64,
        FEATURE_DIM = 27 // LF Pos (3) + RF Pos (3) + LF Vel (3) + RF Vel (3) + Hip Vel (3) + Traj Pos (6) + Traj Dir (6)
    };

    MotionDatabase() = default;

    bool BuildFromAnimations(
        const OzzSkeleton& skeleton,
        const std::vector<OzzAnimation>& animations,
        const MotionBoneMapping& mapping,
        const MotionWeights& weights = MotionWeights()
    );

    bool Save(const std::string& filepath) const;
    bool Load(const std::string& filepath);

    int GetNumFrames() const { return static_cast<int>(m_frames.size()); }
    int GetNumFeatures() const { return FEATURE_DIM; }
    int GetNumAnimations() const { return static_cast<int>(m_animNames.size()); }
    const std::string& GetAnimationName(int idx) const { return m_animNames[idx]; }

    const MotionFrameData& GetFrame(int frameIdx) const { return m_frames[frameIdx]; }
    const std::vector<MotionFrameData>& GetFrames() const { return m_frames; }

    const std::vector<int>& GetRangeStarts() const { return m_rangeStarts; }
    const std::vector<int>& GetRangeStops() const { return m_rangeStops; }

    const float* GetFeaturesRow(int frameIdx) const { return &m_features[frameIdx * FEATURE_DIM]; }
    const std::vector<float>& GetFeatures() const { return m_features; }
    const std::vector<float>& GetFeaturesOffset() const { return m_featuresOffset; }
    const std::vector<float>& GetFeaturesScale() const { return m_featuresScale; }

    const std::vector<float>& GetBoundSmMin() const { return m_boundSmMin; }
    const std::vector<float>& GetBoundSmMax() const { return m_boundSmMax; }
    const std::vector<float>& GetBoundLrMin() const { return m_boundLrMin; }
    const std::vector<float>& GetBoundLrMax() const { return m_boundLrMax; }

    void NormalizeQuery(const float* rawQuery, float* outNormalizedQuery) const;

private:
    std::vector<std::string> m_animNames;
    std::vector<int> m_rangeStarts;
    std::vector<int> m_rangeStops;

    std::vector<MotionFrameData> m_frames;

    // Feature matrix: N x FEATURE_DIM
    std::vector<float> m_features;
    std::vector<float> m_featuresOffset;
    std::vector<float> m_featuresScale;

    // Acceleration Bounding Boxes
    std::vector<float> m_boundSmMin;
    std::vector<float> m_boundSmMax;
    std::vector<float> m_boundLrMin;
    std::vector<float> m_boundLrMax;

    void BuildBounds();
    void NormalizeFeatures(const MotionWeights& weights);
};

} // namespace EngineAnim
