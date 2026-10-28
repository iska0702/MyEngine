#include "MotionMatcher.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace EngineAnim {

MotionMatchResult MotionMatcher::Search(
    const MotionDatabase& db,
    const float* normalizedQuery,
    int currentFrameIndex,
    float transitionCost,
    int ignoreSurrounding
) {
    MotionMatchResult result;
    int numFrames = db.GetNumFrames();
    if (numFrames == 0 || !normalizedQuery) return result;

    const int fDim = MotionDatabase::FEATURE_DIM;
    const auto& features = db.GetFeatures();
    const auto& boundSmMin = db.GetBoundSmMin();
    const auto& boundSmMax = db.GetBoundSmMax();
    const auto& boundLrMin = db.GetBoundLrMin();
    const auto& boundLrMax = db.GetBoundLrMax();

    float bestCost = 1e9f;
    int bestIndex = -1;

    // 1. Cost of current frame without transition penalty
    if (currentFrameIndex >= 0 && currentFrameIndex < numFrames) {
        float currCost = 0.0f;
        const float* featRow = db.GetFeaturesRow(currentFrameIndex);
        for (int j = 0; j < fDim; ++j) {
            float d = normalizedQuery[j] - featRow[j];
            currCost += d * d;
        }
        bestCost = currCost;
        bestIndex = currentFrameIndex;
    }

    int nBoundLr = (numFrames + MotionDatabase::BOUND_LR_SIZE - 1) / MotionDatabase::BOUND_LR_SIZE;

    // 2. Large box scan
    for (int iLr = 0; iLr < nBoundLr; ++iLr) {
        float lrDist = 0.0f;
        const float* bMin = &boundLrMin[iLr * fDim];
        const float* bMax = &boundLrMax[iLr * fDim];

        for (int j = 0; j < fDim; ++j) {
            float q = normalizedQuery[j];
            if (q < bMin[j]) {
                float d = bMin[j] - q;
                lrDist += d * d;
            } else if (q > bMax[j]) {
                float d = q - bMax[j];
                lrDist += d * d;
            }
        }

        if (lrDist >= bestCost) continue;

        // 3. Small box scan inside this large box
        int smStart = (iLr * MotionDatabase::BOUND_LR_SIZE) / MotionDatabase::BOUND_SM_SIZE;
        int smEnd = std::min(
            ((iLr + 1) * MotionDatabase::BOUND_LR_SIZE) / MotionDatabase::BOUND_SM_SIZE,
            (numFrames + MotionDatabase::BOUND_SM_SIZE - 1) / MotionDatabase::BOUND_SM_SIZE
        );

        for (int iSm = smStart; iSm < smEnd; ++iSm) {
            float smDist = 0.0f;
            const float* sMin = &boundSmMin[iSm * fDim];
            const float* sMax = &boundSmMax[iSm * fDim];

            for (int j = 0; j < fDim; ++j) {
                float q = normalizedQuery[j];
                if (q < sMin[j]) {
                    float d = sMin[j] - q;
                    smDist += d * d;
                } else if (q > sMax[j]) {
                    float d = q - sMax[j];
                    smDist += d * d;
                }
            }

            if (smDist >= bestCost) continue;

            // 4. Exact frame scan
            int fStart = iSm * MotionDatabase::BOUND_SM_SIZE;
            int fEnd = std::min(fStart + MotionDatabase::BOUND_SM_SIZE, numFrames);

            for (int f = fStart; f < fEnd; ++f) {
                if (currentFrameIndex >= 0 && std::abs(f - currentFrameIndex) < ignoreSurrounding) {
                    continue;
                }

                const float* featRow = &features[f * fDim];
                float cost = transitionCost;

                for (int j = 0; j < fDim; ++j) {
                    float d = normalizedQuery[j] - featRow[j];
                    cost += d * d;
                    if (cost >= bestCost) break;
                }

                if (cost < bestCost) {
                    bestCost = cost;
                    bestIndex = f;
                }
            }
        }
    }

    if (bestIndex >= 0) {
        const auto& frameData = db.GetFrame(bestIndex);
        result.frameIndex = bestIndex;
        result.animIndex = frameData.animIndex;
        result.time = frameData.time;
        result.cost = bestCost;
    }

    return result;
}

void MotionMatcher::InertializeTransition(
    std::vector<Transform>& inOutOffsets,
    std::vector<Transform>& inOutOffsetVels,
    const std::vector<Transform>& srcPose,
    const std::vector<Transform>& dstPose
) {
    size_t numBones = dstPose.size();
    inOutOffsets.resize(numBones);
    inOutOffsetVels.resize(numBones);

    for (size_t i = 0; i < numBones; ++i) {
        if (i < srcPose.size()) {
            inOutOffsets[i].translation = (srcPose[i].translation - dstPose[i].translation);
            inOutOffsets[i].rotation = (srcPose[i].rotation * dstPose[i].rotation.Inverse()).Normalized();
            inOutOffsets[i].scale = (srcPose[i].scale - dstPose[i].scale);
        } else {
            inOutOffsets[i] = Transform::Identity();
        }
        inOutOffsetVels[i] = Transform(Vec3(0, 0, 0), Quat::Identity(), Vec3(0, 0, 0));
    }
}

void MotionMatcher::InertializeUpdate(
    std::vector<Transform>& inOutPose,
    std::vector<Transform>& inOutOffsets,
    std::vector<Transform>& inOutOffsetVels,
    const std::vector<Transform>& inputPose,
    float halflife,
    float dt
) {
    size_t numBones = inputPose.size();
    inOutPose.resize(numBones);
    if (inOutOffsets.size() < numBones) inOutOffsets.resize(numBones, Transform::Identity());
    if (inOutOffsetVels.size() < numBones) inOutOffsetVels.resize(numBones, Transform(Vec3(0, 0, 0), Quat::Identity(), Vec3(0, 0, 0)));

    float decay = std::exp(- (4.0f * 0.69314718f / (halflife + 1e-4f)) * dt);

    for (size_t i = 0; i < numBones; ++i) {
        // Translation decay
        inOutOffsets[i].translation = inOutOffsets[i].translation * decay;
        inOutPose[i].translation = inputPose[i].translation + inOutOffsets[i].translation;

        // Rotation decay
        inOutOffsets[i].rotation = Slerp(Quat::Identity(), inOutOffsets[i].rotation, decay).Normalized();
        inOutPose[i].rotation = (inOutOffsets[i].rotation * inputPose[i].rotation).Normalized();

        // Scale decay
        inOutOffsets[i].scale = inOutOffsets[i].scale * decay;
        inOutPose[i].scale = inputPose[i].scale + inOutOffsets[i].scale;
    }
}

} // namespace EngineAnim
