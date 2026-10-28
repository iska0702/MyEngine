#pragma once
#include "../AnimationCommon.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace EngineAnim {

struct OzzJoint {
    std::string name;
    int16_t parentIndex = -1;
    Transform restPose;
};

class OzzSkeleton {
public:
    OzzSkeleton() = default;

    void SetJoints(std::vector<OzzJoint> joints);
    
    int GetNumJoints() const { return static_cast<int>(m_joints.size()); }
    const OzzJoint& GetJoint(int index) const { return m_joints[index]; }
    const std::vector<OzzJoint>& GetJoints() const { return m_joints; }
    int FindJointIndex(const std::string& name) const;
    int16_t GetParentIndex(int index) const { return m_joints[index].parentIndex; }
    
    const std::vector<Matrix4x4>& GetBindPoseModelMatrices() const { return m_bindPoseModelMatrices; }
    const std::vector<Matrix4x4>& GetInverseBindPoseMatrices() const { return m_inverseBindMatrices; }

private:
    std::vector<OzzJoint> m_joints;
    std::unordered_map<std::string, int> m_nameToIndex;
    std::vector<Matrix4x4> m_bindPoseModelMatrices;
    std::vector<Matrix4x4> m_inverseBindMatrices;

    void ComputeBindPoseMatrices();
};

struct OzzKeyframeTranslation {
    float time = 0.0f;
    Vec3 value = Vec3(0, 0, 0);
};

struct OzzKeyframeRotation {
    float time = 0.0f;
    Quat value = Quat::Identity();
};

struct OzzKeyframeScale {
    float time = 0.0f;
    Vec3 value = Vec3(1, 1, 1);
};

struct OzzJointTrack {
    std::vector<OzzKeyframeTranslation> translations;
    std::vector<OzzKeyframeRotation> rotations;
    std::vector<OzzKeyframeScale> scales;

    Transform Sample(float time, const Transform& fallback) const;
};

class OzzAnimation {
public:
    OzzAnimation() = default;

    void SetName(const std::string& name) { m_name = name; }
    const std::string& GetName() const { return m_name; }

    void SetDuration(float duration) { m_duration = duration; }
    float GetDuration() const { return m_duration; }

    void SetTracks(std::vector<OzzJointTrack> tracks) { m_tracks = std::move(tracks); }
    const std::vector<OzzJointTrack>& GetTracks() const { return m_tracks; }
    int GetNumTracks() const { return static_cast<int>(m_tracks.size()); }

    Transform SampleTrack(int trackIndex, float time, const Transform& fallback) const;

private:
    std::string m_name;
    float m_duration = 0.0f;
    std::vector<OzzJointTrack> m_tracks;
};

class OzzSamplingJob {
public:
    const OzzAnimation* animation = nullptr;
    const OzzSkeleton* skeleton = nullptr;
    float time = 0.0f;
    std::vector<Transform>* outputLocalTransforms = nullptr;

    bool Run() const;
};

struct OzzBlendLayer {
    const std::vector<Transform>* localTransforms = nullptr;
    float weight = 1.0f;
};

class OzzBlendingJob {
public:
    std::vector<OzzBlendLayer> layers;
    const OzzSkeleton* skeleton = nullptr;
    std::vector<Transform>* outputLocalTransforms = nullptr;

    bool Run() const;
};

class OzzLocalToModelJob {
public:
    const OzzSkeleton* skeleton = nullptr;
    const std::vector<Transform>* inputLocalTransforms = nullptr;
    std::vector<Matrix4x4>* outputModelMatrices = nullptr;
    Transform rootTransform = Transform::Identity();

    bool Run() const;
};

} // namespace EngineAnim
