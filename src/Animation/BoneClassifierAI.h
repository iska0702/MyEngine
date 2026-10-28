#pragma once
#include "HumanoidBoneDef.h"
#include "Ozz/OzzRuntime.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <utility>

namespace EngineAnim {

struct HumanoidBoneMapping {
    std::unordered_map<HumanoidBone, int> boneToJoint;
    std::unordered_map<int, HumanoidBone> jointToBone;
    std::unordered_map<int, float> jointConfidence;

    int GetJoint(HumanoidBone bone) const {
        auto it = boneToJoint.find(bone);
        return (it != boneToJoint.end()) ? it->second : -1;
    }

    HumanoidBone GetHumanoidBone(int jointIdx) const {
        auto it = jointToBone.find(jointIdx);
        return (it != jointToBone.end()) ? it->second : HumanoidBone::None;
    }

    float GetConfidence(int jointIdx) const {
        auto it = jointConfidence.find(jointIdx);
        return (it != jointConfidence.end()) ? it->second : 0.0f;
    }

    bool IsMapped(HumanoidBone bone) const {
        auto it = boneToJoint.find(bone);
        return it != boneToJoint.end() && it->second >= 0;
    }

    void SetMapping(HumanoidBone bone, int jointIdx, float confidence = 1.0f) {
        if (bone == HumanoidBone::None) {
            if (jointIdx >= 0) {
                auto it = jointToBone.find(jointIdx);
                if (it != jointToBone.end()) {
                    boneToJoint.erase(it->second);
                    jointToBone.erase(it);
                    jointConfidence.erase(jointIdx);
                }
            }
            return;
        }

        // If bone was previously mapped to another joint, clean it
        auto itOld = boneToJoint.find(bone);
        if (itOld != boneToJoint.end() && itOld->second != jointIdx) {
            jointToBone.erase(itOld->second);
            jointConfidence.erase(itOld->second);
        }

        // If jointIdx was previously mapped to another bone, clean it
        if (jointIdx >= 0) {
            auto itJointOld = jointToBone.find(jointIdx);
            if (itJointOld != jointToBone.end() && itJointOld->second != bone) {
                boneToJoint.erase(itJointOld->second);
            }

            boneToJoint[bone] = jointIdx;
            jointToBone[jointIdx] = bone;
            jointConfidence[jointIdx] = confidence;
        } else {
            boneToJoint.erase(bone);
        }
    }

    void Clear() {
        boneToJoint.clear();
        jointToBone.clear();
        jointConfidence.clear();
    }
};

struct BoneTrainingSample {
    std::string skeletonName;
    std::string boneName;
    int16_t parentIndex = -1;
    float relativeY = 0.5f;
    float relativeX = 0.0f;
    float relativeZ = 0.0f;
    int depth = 0;
    HumanoidBone targetBone = HumanoidBone::None;
};

struct TrainMetrics {
    int epochsRun = 0;
    float initialLoss = 0.0f;
    float finalLoss = 0.0f;
    float accuracy = 0.0f;
    int sampleCount = 0;
    int vocabularySize = 0;
};

class BoneClassifierAI {
public:
    static BoneClassifierAI& Get();

    void Initialize();

    // AI Inference
    HumanoidBoneMapping ClassifySkeleton(const OzzSkeleton& skeleton);
    std::vector<std::pair<HumanoidBone, float>> PredictBoneCandidates(
        const std::string& boneName,
        int16_t parentIndex = -1,
        float relativeY = 0.5f,
        float relativeX = 0.0f,
        float relativeZ = 0.0f,
        int depth = 0,
        const OzzSkeleton* skeleton = nullptr
    );
    float CalculateOverallConfidence(const HumanoidBoneMapping& mapping) const;

    // AI Training & Learning
    void AddTrainingSample(const BoneTrainingSample& sample);
    void AddSkeletonToDataset(const std::string& skeletonName, const OzzSkeleton& skeleton, const HumanoidBoneMapping& mapping);
    void LearnRule(const std::string& tokenOrSubstring, HumanoidBone targetBone, float weightBoost = 5.0f);
    void LearnCorrection(const std::string& boneName, HumanoidBone correctBone, const OzzSkeleton* skeleton = nullptr, int jointIdx = -1);

    TrainMetrics TrainModel(int epochs = 40, float learningRate = 0.08f);
    void ResetToDefaults();

    // Persistence
    bool SaveModel(const std::string& path = "Config/bone_ai_model.json");
    bool LoadModel(const std::string& path = "Config/bone_ai_model.json");
    bool LoadModelFromString(const std::string& jsonContent);

    // Dataset & Model Accessors
    const std::vector<BoneTrainingSample>& GetTrainingDataset() const { return m_dataset; }
    size_t GetVocabularySize() const { return m_tokenWeights.size(); }
    const TrainMetrics& GetLastTrainMetrics() const { return m_lastMetrics; }
    bool IsAutoLearnEnabled() const { return m_autoLearn; }
    void SetAutoLearnEnabled(bool enabled) { m_autoLearn = enabled; }

    // Helpers
    static std::vector<std::string> TokenizeBoneName(const std::string& name);

private:
    BoneClassifierAI();

    // Learned token weight tables: token -> map<HumanoidBone, weight>
    std::unordered_map<std::string, std::unordered_map<int16_t, float>> m_tokenWeights;
    std::unordered_map<int16_t, float> m_boneBiases;
    std::vector<BoneTrainingSample> m_dataset;
    TrainMetrics m_lastMetrics;
    bool m_autoLearn = true;
    bool m_initialized = false;

    void SeedPretrainedWeights();
    void SeedPretrainedDataset();
    float ComputeScore(const std::vector<std::string>& tokens, HumanoidBone bone, float relY, float relX, int depth, int16_t parentBoneId) const;
    void NormalizeScores(std::vector<std::pair<HumanoidBone, float>>& scores) const;
};

} // namespace EngineAnim
