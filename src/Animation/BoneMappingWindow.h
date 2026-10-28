#pragma once
#include "HumanoidBoneDef.h"
#include "BoneClassifierAI.h"
#include "CharacterAsset.h"
#include <memory>
#include <string>
#include <vector>

class Renderer;

namespace EngineAnim {

class BoneMappingWindow {
public:
    static BoneMappingWindow& Get();

    void Initialize(Renderer* renderer);
    void Draw(Renderer* renderer);

    bool IsOpen() const { return m_isOpen; }
    void SetOpen(bool open) { m_isOpen = open; }
    void ToggleOpen() { m_isOpen = !m_isOpen; }

    void SetActiveCharacter(std::shared_ptr<CharacterAsset> character);
    void RunRecognitionOnActiveCharacter();
    void ApplyMappingToCharacter();

private:
    BoneMappingWindow();

    bool m_isOpen = true;
    Renderer* m_renderer = nullptr;
    std::shared_ptr<CharacterAsset> m_activeCharacter = nullptr;
    HumanoidBoneMapping m_currentMapping;

    // UI state
    int m_selectedCategory = -1; // -1 = All
    char m_slotSearchFilter[128] = "";
    char m_boneSearchFilter[128] = "";
    int m_selectedJointIdx = -1;
    HumanoidBone m_selectedHumanoidBone = HumanoidBone::None;

    // Training UI state
    int m_trainEpochs = 50;
    float m_trainLearningRate = 0.08f;
    char m_customRuleToken[64] = "";
    int m_customRuleTargetBone = 0;
    float m_customRuleWeight = 10.0f;
    std::string m_statusMessage = "Ready";
    float m_statusMessageTimer = 0.0f;

    void DrawHeader();
    void DrawHumanoidSlotsTab();
    void DrawSkeletonBonesTab();
    void DrawTrainingStudioTab();
    void DrawBoneSelectionPopup(HumanoidBone bone);

    void ShowStatusMessage(const std::string& msg, float duration = 4.0f);
};

} // namespace EngineAnim
