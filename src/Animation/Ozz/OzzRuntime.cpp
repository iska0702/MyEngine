#include "OzzRuntime.h"
#include <algorithm>

namespace EngineAnim {

void OzzSkeleton::SetJoints(std::vector<OzzJoint> joints) {
    m_joints = std::move(joints);
    m_nameToIndex.clear();
    for (int i = 0; i < static_cast<int>(m_joints.size()); ++i) {
        m_nameToIndex[m_joints[i].name] = i;
    }
    ComputeBindPoseMatrices();
}

int OzzSkeleton::FindJointIndex(const std::string& name) const {
    auto it = m_nameToIndex.find(name);
    if (it != m_nameToIndex.end()) {
        return it->second;
    }
    return -1;
}

void OzzSkeleton::ComputeBindPoseMatrices() {
    int n = GetNumJoints();
    m_bindPoseModelMatrices.resize(n);
    m_inverseBindMatrices.resize(n);

    for (int i = 0; i < n; ++i) {
        XMMATRIX localMat = m_joints[i].restPose.ToMatrix();
        int16_t parent = m_joints[i].parentIndex;
        if (parent >= 0 && parent < i) {
            XMMATRIX parentMat = m_bindPoseModelMatrices[parent].ToXM();
            XMMATRIX modelMat = localMat * parentMat; // DirectX row-major (v * local * parent)
            m_bindPoseModelMatrices[i] = Matrix4x4(modelMat);
        } else {
            m_bindPoseModelMatrices[i] = Matrix4x4(localMat);
        }

        XMMATRIX invBind = XMMatrixInverse(nullptr, m_bindPoseModelMatrices[i].ToXM());
        m_inverseBindMatrices[i] = Matrix4x4(invBind);
    }
}

Transform OzzJointTrack::Sample(float time, const Transform& fallback) const {
    Transform result = fallback;

    // 1. Translations
    if (!translations.empty()) {
        if (translations.size() == 1 || time <= translations.front().time) {
            result.translation = translations.front().value;
        } else if (time >= translations.back().time) {
            result.translation = translations.back().value;
        } else {
            // Binary search or linear search
            size_t idx = 0;
            while (idx + 1 < translations.size() && translations[idx + 1].time < time) {
                idx++;
            }
            float t0 = translations[idx].time;
            float t1 = translations[idx + 1].time;
            float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            result.translation = Lerp(translations[idx].value, translations[idx + 1].value, alpha);
        }
    }

    // 2. Rotations
    if (!rotations.empty()) {
        if (rotations.size() == 1 || time <= rotations.front().time) {
            result.rotation = rotations.front().value;
        } else if (time >= rotations.back().time) {
            result.rotation = rotations.back().value;
        } else {
            size_t idx = 0;
            while (idx + 1 < rotations.size() && rotations[idx + 1].time < time) {
                idx++;
            }
            float t0 = rotations[idx].time;
            float t1 = rotations[idx + 1].time;
            float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            result.rotation = Slerp(rotations[idx].value, rotations[idx + 1].value, alpha);
        }
    }

    // 3. Scales
    if (!scales.empty()) {
        if (scales.size() == 1 || time <= scales.front().time) {
            result.scale = scales.front().value;
        } else if (time >= scales.back().time) {
            result.scale = scales.back().value;
        } else {
            size_t idx = 0;
            while (idx + 1 < scales.size() && scales[idx + 1].time < time) {
                idx++;
            }
            float t0 = scales[idx].time;
            float t1 = scales[idx + 1].time;
            float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            result.scale = Lerp(scales[idx].value, scales[idx + 1].value, alpha);
        }
    }

    return result;
}

Transform OzzAnimation::SampleTrack(int trackIndex, float time, const Transform& fallback) const {
    if (trackIndex >= 0 && trackIndex < static_cast<int>(m_tracks.size())) {
        return m_tracks[trackIndex].Sample(time, fallback);
    }
    return fallback;
}

bool OzzSamplingJob::Run() const {
    if (!skeleton || !outputLocalTransforms) {
        return false;
    }

    int numJoints = skeleton->GetNumJoints();
    outputLocalTransforms->resize(numJoints);

    if (!animation) {
        for (int i = 0; i < numJoints; ++i) {
            (*outputLocalTransforms)[i] = skeleton->GetJoint(i).restPose;
        }
        return true;
    }

    float animDuration = animation->GetDuration();
    float clampedTime = (animDuration > 0.0f) ? std::fmod(time, animDuration) : 0.0f;
    if (clampedTime < 0.0f) clampedTime += animDuration;

    for (int i = 0; i < numJoints; ++i) {
        const Transform& restPose = skeleton->GetJoint(i).restPose;
        if (i < animation->GetNumTracks()) {
            (*outputLocalTransforms)[i] = animation->SampleTrack(i, clampedTime, restPose);
        } else {
            (*outputLocalTransforms)[i] = restPose;
        }
    }

    return true;
}

bool OzzBlendingJob::Run() const {
    if (!skeleton || !outputLocalTransforms || layers.empty()) {
        return false;
    }

    int numJoints = skeleton->GetNumJoints();
    outputLocalTransforms->resize(numJoints);

    float totalWeight = 0.0f;
    for (const auto& layer : layers) {
        if (layer.localTransforms && layer.localTransforms->size() == static_cast<size_t>(numJoints) && layer.weight > 0.0f) {
            totalWeight += layer.weight;
        }
    }

    if (totalWeight <= 1e-6f) {
        for (int i = 0; i < numJoints; ++i) {
            (*outputLocalTransforms)[i] = skeleton->GetJoint(i).restPose;
        }
        return true;
    }

    for (int i = 0; i < numJoints; ++i) {
        Vec3 accTrans(0, 0, 0);
        Vec3 accScale(0, 0, 0);
        Quat baseRot = Quat::Identity();
        bool first = true;

        for (const auto& layer : layers) {
            if (!layer.localTransforms || layer.localTransforms->size() != static_cast<size_t>(numJoints) || layer.weight <= 0.0f) {
                continue;
            }
            float normWeight = layer.weight / totalWeight;
            const Transform& t = (*layer.localTransforms)[i];

            accTrans += t.translation * normWeight;
            accScale += t.scale * normWeight;

            if (first) {
                baseRot = t.rotation;
                first = false;
            } else {
                baseRot = Slerp(baseRot, t.rotation, normWeight);
            }
        }

        (*outputLocalTransforms)[i] = Transform(accTrans, baseRot.Normalized(), accScale);
    }

    return true;
}

bool OzzLocalToModelJob::Run() const {
    if (!skeleton || !inputLocalTransforms || !outputModelMatrices) {
        return false;
    }

    int numJoints = skeleton->GetNumJoints();
    if (static_cast<int>(inputLocalTransforms->size()) < numJoints) {
        return false;
    }

    outputModelMatrices->resize(numJoints);
    XMMATRIX rootMat = rootTransform.ToMatrix();

    for (int i = 0; i < numJoints; ++i) {
        XMMATRIX localMat = (*inputLocalTransforms)[i].ToMatrix();
        int16_t parent = skeleton->GetParentIndex(i);

        if (parent >= 0 && parent < i) {
            XMMATRIX parentMat = (*outputModelMatrices)[parent].ToXM();
            XMMATRIX modelMat = localMat * parentMat; // DirectX row-major
            (*outputModelMatrices)[i] = Matrix4x4(modelMat);
        } else {
            XMMATRIX modelMat = localMat * rootMat;
            (*outputModelMatrices)[i] = Matrix4x4(modelMat);
        }
    }

    return true;
}

} // namespace EngineAnim
