#include "MotionDatabase.h"
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace EngineAnim {

namespace {
    const uint32_t MDB_MAGIC = 0x3142444D; // "MDB1"
    const uint32_t MDB_VERSION = 1;

    void FKFull(
        std::vector<Vec3>& outGlobalPos,
        std::vector<Quat>& outGlobalRot,
        const std::vector<Transform>& localTransforms,
        const OzzSkeleton& skeleton
    ) {
        int n = skeleton.GetNumJoints();
        outGlobalPos.resize(n);
        outGlobalRot.resize(n);

        for (int i = 0; i < n; ++i) {
            int16_t parent = skeleton.GetParentIndex(i);
            if (parent < 0 || parent >= i) {
                outGlobalPos[i] = localTransforms[i].translation;
                outGlobalRot[i] = localTransforms[i].rotation;
            } else {
                Vec3 pPos = outGlobalPos[parent];
                Quat pRot = outGlobalRot[parent];
                outGlobalPos[i] = pPos + Rotate(pRot, localTransforms[i].translation);
                outGlobalRot[i] = (pRot * localTransforms[i].rotation).Normalized();
            }
        }
    }
}

bool MotionDatabase::BuildFromAnimations(
    const OzzSkeleton& skeleton,
    const std::vector<OzzAnimation>& animations,
    const MotionBoneMapping& mapping,
    const MotionWeights& weights
) {
    if (skeleton.GetNumJoints() == 0 || animations.empty()) return false;

    m_animNames.clear();
    m_rangeStarts.clear();
    m_rangeStops.clear();
    m_frames.clear();

    const float fps = 60.0f;
    const float dt = 1.0f / fps;

    OzzSamplingJob sampleJob;
    sampleJob.skeleton = &skeleton;

    // 1. Sample all animations and store frames
    for (size_t animIdx = 0; animIdx < animations.size(); ++animIdx) {
        const auto& anim = animations[animIdx];
        float duration = anim.GetDuration();
        if (duration <= 0.0f) continue;

        int numFrames = static_cast<int>(std::ceil(duration * fps));
        if (numFrames < 2) numFrames = 2;

        int rangeStart = static_cast<int>(m_frames.size());
        m_animNames.push_back(anim.GetName());
        m_rangeStarts.push_back(rangeStart);

        sampleJob.animation = &anim;

        for (int f = 0; f < numFrames; ++f) {
            float t = f * dt;
            MotionFrameData frame;
            frame.animIndex = static_cast<int>(animIdx);
            frame.time = t;

            sampleJob.time = t;
            sampleJob.outputLocalTransforms = &frame.localTransforms;
            sampleJob.Run();

            // Extract root transform (joint 0)
            if (!frame.localTransforms.empty()) {
                frame.rootPosition = frame.localTransforms[0].translation;
                frame.rootRotation = frame.localTransforms[0].rotation;
            }

            m_frames.push_back(std::move(frame));
        }

        int rangeStop = static_cast<int>(m_frames.size());
        m_rangeStops.push_back(rangeStop);
    }

    int totalFrames = static_cast<int>(m_frames.size());
    if (totalFrames == 0) return false;

    // 2. Compute root velocities & joint global positions for all frames
    std::vector<std::vector<Vec3>> allGlobalPos(totalFrames);
    std::vector<std::vector<Quat>> allGlobalRot(totalFrames);

    for (int i = 0; i < totalFrames; ++i) {
        FKFull(allGlobalPos[i], allGlobalRot[i], m_frames[i].localTransforms, skeleton);
    }

    // Compute velocities using finite difference
    for (size_t r = 0; r < m_rangeStarts.size(); ++r) {
        int start = m_rangeStarts[r];
        int stop = m_rangeStops[r];

        for (int i = start; i < stop; ++i) {
            int prevIdx = (i > start) ? (i - 1) : i;
            int nextIdx = (i + 1 < stop) ? (i + 1) : i;
            float timeSpan = (nextIdx - prevIdx) * dt;
            if (timeSpan <= 0.0f) timeSpan = dt;

            // Root velocity
            m_frames[i].rootVelocity = (m_frames[nextIdx].rootPosition - m_frames[prevIdx].rootPosition) / timeSpan;

            // Root angular velocity
            Quat rPrevInv = m_frames[prevIdx].rootRotation.Inverse();
            Quat diffRot = (m_frames[nextIdx].rootRotation * rPrevInv).Normalized();
            m_frames[i].rootAngularVelocity = Vec3(diffRot.x * 2.0f / timeSpan, diffRot.y * 2.0f / timeSpan, diffRot.z * 2.0f / timeSpan);
        }
    }

    // 3. Extract Feature Matrix
    m_features.resize(totalFrames * FEATURE_DIM);

    int numJoints = skeleton.GetNumJoints();
    int lfIdx = (mapping.leftFootJoint >= 0 && mapping.leftFootJoint < numJoints) ? mapping.leftFootJoint : 0;
    int rfIdx = (mapping.rightFootJoint >= 0 && mapping.rightFootJoint < numJoints) ? mapping.rightFootJoint : 0;
    int hipIdx = (mapping.hipsJoint >= 0 && mapping.hipsJoint < numJoints) ? mapping.hipsJoint : 0;

    for (size_t r = 0; r < m_rangeStarts.size(); ++r) {
        int start = m_rangeStarts[r];
        int stop = m_rangeStops[r];

        for (int i = start; i < stop; ++i) {
            float* feat = &m_features[i * FEATURE_DIM];
            int featIdx = 0;

            Quat rootRotInv = m_frames[i].rootRotation.Inverse();
            Vec3 rootPos = m_frames[i].rootPosition;

            // Feature 1: Left Foot Position relative to Root
            Vec3 lfPosRel = Rotate(rootRotInv, allGlobalPos[i][lfIdx] - rootPos);
            feat[featIdx++] = lfPosRel.x;
            feat[featIdx++] = lfPosRel.y;
            feat[featIdx++] = lfPosRel.z;

            // Feature 2: Right Foot Position relative to Root
            Vec3 rfPosRel = Rotate(rootRotInv, allGlobalPos[i][rfIdx] - rootPos);
            feat[featIdx++] = rfPosRel.x;
            feat[featIdx++] = rfPosRel.y;
            feat[featIdx++] = rfPosRel.z;

            // Feature 3: Left Foot Velocity relative to Root
            int prevIdx = (i > start) ? (i - 1) : i;
            int nextIdx = (i + 1 < stop) ? (i + 1) : i;
            float timeSpan = (nextIdx - prevIdx) * dt;
            if (timeSpan <= 0.0f) timeSpan = dt;

            Vec3 lfVelWorld = (allGlobalPos[nextIdx][lfIdx] - allGlobalPos[prevIdx][lfIdx]) / timeSpan;
            Vec3 lfVelRel = Rotate(rootRotInv, lfVelWorld);
            feat[featIdx++] = lfVelRel.x;
            feat[featIdx++] = lfVelRel.y;
            feat[featIdx++] = lfVelRel.z;

            // Feature 4: Right Foot Velocity relative to Root
            Vec3 rfVelWorld = (allGlobalPos[nextIdx][rfIdx] - allGlobalPos[prevIdx][rfIdx]) / timeSpan;
            Vec3 rfVelRel = Rotate(rootRotInv, rfVelWorld);
            feat[featIdx++] = rfVelRel.x;
            feat[featIdx++] = rfVelRel.y;
            feat[featIdx++] = rfVelRel.z;

            // Feature 5: Hip Velocity relative to Root
            Vec3 hipVelWorld = (allGlobalPos[nextIdx][hipIdx] - allGlobalPos[prevIdx][hipIdx]) / timeSpan;
            Vec3 hipVelRel = Rotate(rootRotInv, hipVelWorld);
            feat[featIdx++] = hipVelRel.x;
            feat[featIdx++] = hipVelRel.y;
            feat[featIdx++] = hipVelRel.z;

            // Feature 6: Trajectory Future Positions (horizons: +20, +40, +60 frames)
            int t0 = std::clamp(i + 20, start, stop - 1);
            int t1 = std::clamp(i + 40, start, stop - 1);
            int t2 = std::clamp(i + 60, start, stop - 1);

            Vec3 trajPos0 = Rotate(rootRotInv, m_frames[t0].rootPosition - rootPos);
            Vec3 trajPos1 = Rotate(rootRotInv, m_frames[t1].rootPosition - rootPos);
            Vec3 trajPos2 = Rotate(rootRotInv, m_frames[t2].rootPosition - rootPos);

            feat[featIdx++] = trajPos0.x;
            feat[featIdx++] = trajPos0.z;
            feat[featIdx++] = trajPos1.x;
            feat[featIdx++] = trajPos1.z;
            feat[featIdx++] = trajPos2.x;
            feat[featIdx++] = trajPos2.z;

            // Feature 7: Trajectory Future Directions (horizons: +20, +40, +60 frames)
            Vec3 fwd(0, 0, 1);
            Vec3 trajDir0 = Rotate(rootRotInv, Rotate(m_frames[t0].rootRotation, fwd));
            Vec3 trajDir1 = Rotate(rootRotInv, Rotate(m_frames[t1].rootRotation, fwd));
            Vec3 trajDir2 = Rotate(rootRotInv, Rotate(m_frames[t2].rootRotation, fwd));

            feat[featIdx++] = trajDir0.x;
            feat[featIdx++] = trajDir0.z;
            feat[featIdx++] = trajDir1.x;
            feat[featIdx++] = trajDir1.z;
            feat[featIdx++] = trajDir2.x;
            feat[featIdx++] = trajDir2.z;
        }
    }

    // 4. Normalize features (Mean & Variance calculation)
    NormalizeFeatures(weights);

    // 5. Build AABB search bounding boxes
    BuildBounds();

    return true;
}

void MotionDatabase::NormalizeFeatures(const MotionWeights& weights) {
    int numFrames = GetNumFrames();
    m_featuresOffset.assign(FEATURE_DIM, 0.0f);
    m_featuresScale.assign(FEATURE_DIM, 1.0f);

    if (numFrames == 0) return;

    // Feature group weights
    float featWeights[FEATURE_DIM];
    int idx = 0;
    // LF Pos (3), RF Pos (3)
    for (int k = 0; k < 6; ++k) featWeights[idx++] = weights.footPosition;
    // LF Vel (3), RF Vel (3)
    for (int k = 0; k < 6; ++k) featWeights[idx++] = weights.footVelocity;
    // Hip Vel (3)
    for (int k = 0; k < 3; ++k) featWeights[idx++] = weights.hipVelocity;
    // Traj Pos (6)
    for (int k = 0; k < 6; ++k) featWeights[idx++] = weights.trajectoryPosition;
    // Traj Dir (6)
    for (int k = 0; k < 6; ++k) featWeights[idx++] = weights.trajectoryDirection;

    // 1. Mean
    for (int f = 0; f < numFrames; ++f) {
        for (int d = 0; d < FEATURE_DIM; ++d) {
            m_featuresOffset[d] += m_features[f * FEATURE_DIM + d] / static_cast<float>(numFrames);
        }
    }

    // 2. Variance
    std::vector<float> variance(FEATURE_DIM, 0.0f);
    for (int f = 0; f < numFrames; ++f) {
        for (int d = 0; d < FEATURE_DIM; ++d) {
            float diff = m_features[f * FEATURE_DIM + d] - m_featuresOffset[d];
            variance[d] += (diff * diff) / static_cast<float>(numFrames);
        }
    }

    // 3. Std & Scale
    for (int d = 0; d < FEATURE_DIM; ++d) {
        float stdDev = std::sqrt(variance[d]);
        if (stdDev < 1e-4f) stdDev = 1.0f;
        m_featuresScale[d] = stdDev / featWeights[d];
    }

    // 4. Apply Normalization
    for (int f = 0; f < numFrames; ++f) {
        for (int d = 0; d < FEATURE_DIM; ++d) {
            m_features[f * FEATURE_DIM + d] = (m_features[f * FEATURE_DIM + d] - m_featuresOffset[d]) / m_featuresScale[d];
        }
    }
}

void MotionDatabase::BuildBounds() {
    int numFrames = GetNumFrames();
    int nBoundSm = (numFrames + BOUND_SM_SIZE - 1) / BOUND_SM_SIZE;
    int nBoundLr = (numFrames + BOUND_LR_SIZE - 1) / BOUND_LR_SIZE;

    m_boundSmMin.assign(nBoundSm * FEATURE_DIM, FLT_MAX);
    m_boundSmMax.assign(nBoundSm * FEATURE_DIM, -FLT_MAX);
    m_boundLrMin.assign(nBoundLr * FEATURE_DIM, FLT_MAX);
    m_boundLrMax.assign(nBoundLr * FEATURE_DIM, -FLT_MAX);

    for (int i = 0; i < numFrames; ++i) {
        int iSm = i / BOUND_SM_SIZE;
        int iLr = i / BOUND_LR_SIZE;

        for (int j = 0; j < FEATURE_DIM; ++j) {
            float val = m_features[i * FEATURE_DIM + j];

            m_boundSmMin[iSm * FEATURE_DIM + j] = std::min(m_boundSmMin[iSm * FEATURE_DIM + j], val);
            m_boundSmMax[iSm * FEATURE_DIM + j] = std::max(m_boundSmMax[iSm * FEATURE_DIM + j], val);

            m_boundLrMin[iLr * FEATURE_DIM + j] = std::min(m_boundLrMin[iLr * FEATURE_DIM + j], val);
            m_boundLrMax[iLr * FEATURE_DIM + j] = std::max(m_boundLrMax[iLr * FEATURE_DIM + j], val);
        }
    }
}

void MotionDatabase::NormalizeQuery(const float* rawQuery, float* outNormalizedQuery) const {
    for (int d = 0; d < FEATURE_DIM; ++d) {
        outNormalizedQuery[d] = (rawQuery[d] - m_featuresOffset[d]) / m_featuresScale[d];
    }
}

bool MotionDatabase::Save(const std::string& filepath) const {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(&MDB_MAGIC), sizeof(MDB_MAGIC));
    out.write(reinterpret_cast<const char*>(&MDB_VERSION), sizeof(MDB_VERSION));

    uint32_t numAnims = static_cast<uint32_t>(m_animNames.size());
    out.write(reinterpret_cast<const char*>(&numAnims), sizeof(numAnims));
    for (uint32_t i = 0; i < numAnims; ++i) {
        uint32_t len = static_cast<uint32_t>(m_animNames[i].size());
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0) out.write(m_animNames[i].data(), len);
        out.write(reinterpret_cast<const char*>(&m_rangeStarts[i]), sizeof(int));
        out.write(reinterpret_cast<const char*>(&m_rangeStops[i]), sizeof(int));
    }

    uint32_t numFrames = static_cast<uint32_t>(m_frames.size());
    out.write(reinterpret_cast<const char*>(&numFrames), sizeof(numFrames));

    for (uint32_t i = 0; i < numFrames; ++i) {
        const auto& f = m_frames[i];
        out.write(reinterpret_cast<const char*>(&f.animIndex), sizeof(f.animIndex));
        out.write(reinterpret_cast<const char*>(&f.time), sizeof(f.time));
        out.write(reinterpret_cast<const char*>(&f.rootPosition), sizeof(f.rootPosition));
        out.write(reinterpret_cast<const char*>(&f.rootRotation), sizeof(f.rootRotation));
        out.write(reinterpret_cast<const char*>(&f.rootVelocity), sizeof(f.rootVelocity));
        out.write(reinterpret_cast<const char*>(&f.rootAngularVelocity), sizeof(f.rootAngularVelocity));

        uint32_t numJoints = static_cast<uint32_t>(f.localTransforms.size());
        out.write(reinterpret_cast<const char*>(&numJoints), sizeof(numJoints));
        if (numJoints > 0) {
            out.write(reinterpret_cast<const char*>(f.localTransforms.data()), numJoints * sizeof(Transform));
        }
    }

    // Features & Offset/Scale
    out.write(reinterpret_cast<const char*>(m_featuresOffset.data()), FEATURE_DIM * sizeof(float));
    out.write(reinterpret_cast<const char*>(m_featuresScale.data()), FEATURE_DIM * sizeof(float));
    out.write(reinterpret_cast<const char*>(m_features.data()), m_features.size() * sizeof(float));

    return true;
}

bool MotionDatabase::Load(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t magic = 0, ver = 0;
    if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
    if (!in.read(reinterpret_cast<char*>(&ver), sizeof(ver))) return false;
    if (magic != MDB_MAGIC) return false;

    uint32_t numAnims = 0;
    if (!in.read(reinterpret_cast<char*>(&numAnims), sizeof(numAnims))) return false;

    m_animNames.resize(numAnims);
    m_rangeStarts.resize(numAnims);
    m_rangeStops.resize(numAnims);

    for (uint32_t i = 0; i < numAnims; ++i) {
        uint32_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        m_animNames[i].resize(len);
        if (len > 0) {
            if (!in.read(&m_animNames[i][0], len)) return false;
        }
        if (!in.read(reinterpret_cast<char*>(&m_rangeStarts[i]), sizeof(int))) return false;
        if (!in.read(reinterpret_cast<char*>(&m_rangeStops[i]), sizeof(int))) return false;
    }

    uint32_t numFrames = 0;
    if (!in.read(reinterpret_cast<char*>(&numFrames), sizeof(numFrames))) return false;
    m_frames.resize(numFrames);

    for (uint32_t i = 0; i < numFrames; ++i) {
        auto& f = m_frames[i];
        if (!in.read(reinterpret_cast<char*>(&f.animIndex), sizeof(f.animIndex))) return false;
        if (!in.read(reinterpret_cast<char*>(&f.time), sizeof(f.time))) return false;
        if (!in.read(reinterpret_cast<char*>(&f.rootPosition), sizeof(f.rootPosition))) return false;
        if (!in.read(reinterpret_cast<char*>(&f.rootRotation), sizeof(f.rootRotation))) return false;
        if (!in.read(reinterpret_cast<char*>(&f.rootVelocity), sizeof(f.rootVelocity))) return false;
        if (!in.read(reinterpret_cast<char*>(&f.rootAngularVelocity), sizeof(f.rootAngularVelocity))) return false;

        uint32_t numJoints = 0;
        if (!in.read(reinterpret_cast<char*>(&numJoints), sizeof(numJoints))) return false;
        f.localTransforms.resize(numJoints);
        if (numJoints > 0) {
            if (!in.read(reinterpret_cast<char*>(f.localTransforms.data()), numJoints * sizeof(Transform))) return false;
        }
    }

    m_featuresOffset.resize(FEATURE_DIM);
    m_featuresScale.resize(FEATURE_DIM);
    m_features.resize(numFrames * FEATURE_DIM);

    if (!in.read(reinterpret_cast<char*>(m_featuresOffset.data()), FEATURE_DIM * sizeof(float))) return false;
    if (!in.read(reinterpret_cast<char*>(m_featuresScale.data()), FEATURE_DIM * sizeof(float))) return false;
    if (!in.read(reinterpret_cast<char*>(m_features.data()), m_features.size() * sizeof(float))) return false;

    BuildBounds();
    return true;
}

} // namespace EngineAnim
