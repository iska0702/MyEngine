#define NOMINMAX
#include "BoneMappingWindow.h"
#include "AnimationSystem.h"
#include "Renderer/Renderer.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

namespace EngineAnim {

BoneMappingWindow& BoneMappingWindow::Get() {
    static BoneMappingWindow s_instance;
    return s_instance;
}

BoneMappingWindow::BoneMappingWindow() {
}

void BoneMappingWindow::Initialize(Renderer* renderer) {
    m_renderer = renderer;
    BoneClassifierAI::Get().Initialize();
}

void BoneMappingWindow::SetActiveCharacter(std::shared_ptr<CharacterAsset> character) {
    m_activeCharacter = character;
    if (m_activeCharacter) {
        RunRecognitionOnActiveCharacter();
    }
}

void BoneMappingWindow::RunRecognitionOnActiveCharacter() {
    if (!m_activeCharacter) return;
    const auto& skel = m_activeCharacter->GetSkeleton();
    if (skel.GetNumJoints() == 0) return;

    m_currentMapping = BoneClassifierAI::Get().ClassifySkeleton(skel);
    ShowStatusMessage("AI successfully classified " + std::to_string(skel.GetNumJoints()) + " bones.");
}

void BoneMappingWindow::ApplyMappingToCharacter() {
    if (!m_activeCharacter) return;

    // Update character motion matching mapping
    auto& mc = m_activeCharacter->GetMotionController();
    auto& mapping = mc.boneMapping;

    int rootJoint = m_currentMapping.GetJoint(HumanoidBone::Root);
    int hipsJoint = m_currentMapping.GetJoint(HumanoidBone::Hips);
    int leftFootJoint = m_currentMapping.GetJoint(HumanoidBone::LeftFoot);
    int rightFootJoint = m_currentMapping.GetJoint(HumanoidBone::RightFoot);
    int leftHandJoint = m_currentMapping.GetJoint(HumanoidBone::LeftHand);
    int rightHandJoint = m_currentMapping.GetJoint(HumanoidBone::RightHand);
    int headJoint = m_currentMapping.GetJoint(HumanoidBone::Head);
    int chestJoint = m_currentMapping.GetJoint(HumanoidBone::Chest);

    if (rootJoint >= 0) mapping.rootJoint = rootJoint;
    if (hipsJoint >= 0) mapping.hipsJoint = hipsJoint;
    if (leftFootJoint >= 0) mapping.leftFootJoint = leftFootJoint;
    if (rightFootJoint >= 0) mapping.rightFootJoint = rightFootJoint;
    if (leftHandJoint >= 0) mapping.leftHandJoint = leftHandJoint;
    if (rightHandJoint >= 0) mapping.rightHandJoint = rightHandJoint;
    if (headJoint >= 0) mapping.headJoint = headJoint;
    if (chestJoint >= 0) mapping.chestJoint = chestJoint;

    ShowStatusMessage("Mapping applied to Character Motion Matching & Rig!");
}

void BoneMappingWindow::ShowStatusMessage(const std::string& msg, float duration) {
    m_statusMessage = msg;
    m_statusMessageTimer = duration;
}

#ifndef ENGINE_RELEASE
void BoneMappingWindow::Draw(Renderer* renderer) {
    if (!m_isOpen) return;

    if (!renderer) renderer = m_renderer;

    // Synchronize active character with AnimationSystem selection if null
    auto selectedChar = AnimationSystem::Get().GetSelectedCharacter();
    if (!m_activeCharacter && selectedChar) {
        SetActiveCharacter(selectedChar);
    } else if (m_activeCharacter != selectedChar && selectedChar) {
        m_activeCharacter = selectedChar;
        RunRecognitionOnActiveCharacter();
    }

    ImGui::SetNextWindowSize(ImVec2(800, 680), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("AI Humanoid Bone Auto-Mapper & Trainer", &m_isOpen)) {
        DrawHeader();
        ImGui::Separator();

        if (ImGui::BeginTabBar("BoneMappingTabs")) {
            if (ImGui::BeginTabItem("Humanoid Slots")) {
                DrawHumanoidSlotsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Skeleton & Categories")) {
                DrawSkeletonBonesTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("AI Training Studio")) {
                DrawTrainingStudioTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void BoneMappingWindow::DrawHeader() {
    auto& animSys = AnimationSystem::Get();
    const auto& characters = animSys.GetCharacters();

    // 1. Character Selector
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Target Character:");
    ImGui::SameLine();

    std::string previewName = m_activeCharacter ? m_activeCharacter->GetName() : "None (Select or Import)";
    if (ImGui::BeginCombo("##CharSelect", previewName.c_str())) {
        for (size_t i = 0; i < characters.size(); ++i) {
            bool isSelected = (m_activeCharacter == characters[i]);
            if (ImGui::Selectable(characters[i]->GetName().c_str(), isSelected)) {
                animSys.SetSelectedCharacterIndex(static_cast<int>(i));
                SetActiveCharacter(characters[i]);
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Import FBX...")) {
        animSys.ImportCharacterDialog(m_renderer);
    }

    // 2. Metrics & Status
    if (m_activeCharacter) {
        const auto& skel = m_activeCharacter->GetSkeleton();
        int totalJoints = skel.GetNumJoints();
        int mappedBones = static_cast<int>(m_currentMapping.jointToBone.size());
        float confidence = BoneClassifierAI::Get().CalculateOverallConfidence(m_currentMapping);

        ImGui::Text("Skeleton Joints: %d  |  Mapped Humanoid Bones: %d / %d", totalJoints, mappedBones, (int)HumanoidBone::Count);
        ImGui::SameLine();

        ImVec4 confColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        if (confidence < 40.0f) confColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        else if (confidence < 75.0f) confColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);

        ImGui::TextColored(confColor, "|  AI Overall Confidence: %.1f%%", confidence);

        // Action Buttons
        if (ImGui::Button("Run AI Recognition", ImVec2(180, 26))) {
            RunRecognitionOnActiveCharacter();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Mapping to Character & Motion Matching", ImVec2(320, 26))) {
            ApplyMappingToCharacter();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Mappings", ImVec2(130, 26))) {
            m_currentMapping.Clear();
            ShowStatusMessage("Mappings cleared.");
        }
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No character selected. Please import or select a character above.");
    }

    if (!m_statusMessage.empty()) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", m_statusMessage.c_str());
    }
}

void BoneMappingWindow::DrawHumanoidSlotsTab() {
    if (!m_activeCharacter) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select a character to view and configure humanoid slots.");
        return;
    }

    const auto& skel = m_activeCharacter->GetSkeleton();

    // Category Filter Buttons
    ImGui::Text("Filter Body Region:");
    ImGui::SameLine();
    if (ImGui::RadioButton("All", m_selectedCategory == -1)) m_selectedCategory = -1;
    ImGui::SameLine();
    if (ImGui::RadioButton("Core", m_selectedCategory == (int)BoneCategory::SpineTorso)) m_selectedCategory = (int)BoneCategory::SpineTorso;
    ImGui::SameLine();
    if (ImGui::RadioButton("Head/Face", m_selectedCategory == (int)BoneCategory::HeadFace)) m_selectedCategory = (int)BoneCategory::HeadFace;
    ImGui::SameLine();
    if (ImGui::RadioButton("Arms", m_selectedCategory == (int)BoneCategory::LeftArm)) m_selectedCategory = (int)BoneCategory::LeftArm;
    ImGui::SameLine();
    if (ImGui::RadioButton("Legs", m_selectedCategory == (int)BoneCategory::LeftLeg)) m_selectedCategory = (int)BoneCategory::LeftLeg;
    ImGui::SameLine();
    if (ImGui::RadioButton("Fingers", m_selectedCategory == (int)BoneCategory::LeftFingers)) m_selectedCategory = (int)BoneCategory::LeftFingers;
    ImGui::SameLine();
    if (ImGui::RadioButton("Twist/Aux", m_selectedCategory == (int)BoneCategory::AuxiliaryTwist)) m_selectedCategory = (int)BoneCategory::AuxiliaryTwist;

    ImGui::InputTextWithHint("##SlotFilter", "Search slot name...", m_slotSearchFilter, sizeof(m_slotSearchFilter));
    std::string searchLower = m_slotSearchFilter;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), [](unsigned char c) { return std::tolower(c); });

    ImGui::Separator();

    // Table of Humanoid Slots
    if (ImGui::BeginTable("HumanoidSlotsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 420))) {
        ImGui::TableSetupColumn("Slot (Bone Role)", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Assigned Bone in 3D Model", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("AI Confidence", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
            HumanoidBone bone = static_cast<HumanoidBone>(i);
            const auto& info = GetHumanoidBoneInfo(bone);

            // Category filter check
            if (m_selectedCategory != -1) {
                if (m_selectedCategory == (int)BoneCategory::LeftArm && info.category != BoneCategory::LeftArm && info.category != BoneCategory::RightArm) continue;
                else if (m_selectedCategory == (int)BoneCategory::LeftLeg && info.category != BoneCategory::LeftLeg && info.category != BoneCategory::RightLeg) continue;
                else if (m_selectedCategory == (int)BoneCategory::LeftFingers && info.category != BoneCategory::LeftFingers && info.category != BoneCategory::RightFingers) continue;
                else if (m_selectedCategory != (int)info.category && m_selectedCategory != (int)BoneCategory::LeftArm && m_selectedCategory != (int)BoneCategory::LeftLeg && m_selectedCategory != (int)BoneCategory::LeftFingers) continue;
            }

            // Search filter check
            if (!searchLower.empty()) {
                std::string nameLower = info.displayName;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) { return std::tolower(c); });
                std::string ruLower = info.displayNameRu;
                std::transform(ruLower.begin(), ruLower.end(), ruLower.begin(), [](unsigned char c) { return std::tolower(c); });
                if (nameLower.find(searchLower) == std::string::npos && ruLower.find(searchLower) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();
            ImGui::PushID(i);

            // Column 1: Slot Name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", info.displayName);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(%s)", info.displayNameRu);

            // Column 2: Required / Optional
            ImGui::TableSetColumnIndex(1);
            if (info.isRequired) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Required");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Optional");
            }

            // Column 3: Assigned Bone Dropdown
            ImGui::TableSetColumnIndex(2);
            int mappedJoint = m_currentMapping.GetJoint(bone);
            std::string jointName = (mappedJoint >= 0 && mappedJoint < skel.GetNumJoints()) ? skel.GetJoint(mappedJoint).name : " [ None / Unassigned ] ";

            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##BoneCombo", jointName.c_str())) {
                if (ImGui::Selectable(" [ None ] ", mappedJoint < 0)) {
                    m_currentMapping.SetMapping(bone, -1);
                }

                for (int j = 0; j < skel.GetNumJoints(); ++j) {
                    const auto& joint = skel.GetJoint(j);
                    bool isSelected = (mappedJoint == j);
                    if (ImGui::Selectable(joint.name.c_str(), isSelected)) {
                        m_currentMapping.SetMapping(bone, j, 1.0f);

                        // If auto-learn enabled, learn from user correction
                        if (BoneClassifierAI::Get().IsAutoLearnEnabled()) {
                            BoneClassifierAI::Get().LearnCorrection(joint.name, bone, &skel, j);
                            ShowStatusMessage("AI learned mapping: " + joint.name + " -> " + info.displayName);
                        }
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Column 4: AI Confidence
            ImGui::TableSetColumnIndex(3);
            if (mappedJoint >= 0) {
                float conf = m_currentMapping.GetConfidence(mappedJoint) * 100.0f;
                ImVec4 cCol = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
                if (conf < 40.0f) cCol = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                else if (conf < 75.0f) cCol = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);

                ImGui::TextColored(cCol, "%.1f%%", conf);
                ImGui::SameLine();
                ImGui::ProgressBar(conf / 100.0f, ImVec2(50, 0), "");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "0.0%");
            }

            // Column 5: Action (Teach / Clear)
            ImGui::TableSetColumnIndex(4);
            if (mappedJoint >= 0) {
                if (ImGui::SmallButton("Teach AI")) {
                    const auto& joint = skel.GetJoint(mappedJoint);
                    BoneClassifierAI::Get().LearnCorrection(joint.name, bone, &skel, mappedJoint);
                    ShowStatusMessage("Trained AI on: " + joint.name + " -> " + info.displayName);
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void BoneMappingWindow::DrawSkeletonBonesTab() {
    if (!m_activeCharacter) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select a character to view skeleton bones.");
        return;
    }

    const auto& skel = m_activeCharacter->GetSkeleton();
    int numJoints = skel.GetNumJoints();

    ImGui::InputTextWithHint("##BoneFilter", "Search skeleton bone name...", m_boneSearchFilter, sizeof(m_boneSearchFilter));
    std::string searchLower = m_boneSearchFilter;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), [](unsigned char c) { return std::tolower(c); });

    ImGui::Separator();

    if (ImGui::BeginTable("SkeletonBonesTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 440))) {
        ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Skeleton Bone Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Parent", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Detected Category", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Mapped Humanoid Role", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Confidence", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < numJoints; ++i) {
            const auto& joint = skel.GetJoint(i);

            // Filter
            if (!searchLower.empty()) {
                std::string jLower = joint.name;
                std::transform(jLower.begin(), jLower.end(), jLower.begin(), [](unsigned char c) { return std::tolower(c); });
                if (jLower.find(searchLower) == std::string::npos) continue;
            }

            ImGui::TableNextRow();
            ImGui::PushID(i);

            // Column 1: Index
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);

            // Column 2: Bone Name
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", joint.name.c_str());

            // Column 3: Parent
            ImGui::TableSetColumnIndex(2);
            if (joint.parentIndex >= 0) {
                ImGui::Text("%d (%s)", joint.parentIndex, skel.GetJoint(joint.parentIndex).name.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f), "Root");
            }

            // Column 4: Category
            ImGui::TableSetColumnIndex(3);
            HumanoidBone mapped = m_currentMapping.GetHumanoidBone(i);
            if (mapped != HumanoidBone::None) {
                const auto& info = GetHumanoidBoneInfo(mapped);
                ImGui::Text("%s", GetCategoryName(info.category, true));
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Extra / Unused");
            }

            // Column 5: Mapped Humanoid Role
            ImGui::TableSetColumnIndex(4);
            std::string roleName = (mapped != HumanoidBone::None) ? GetHumanoidBoneInfo(mapped).displayName : "[ None ]";
            if (ImGui::BeginCombo("##RoleCombo", roleName.c_str())) {
                if (ImGui::Selectable("[ None ]", mapped == HumanoidBone::None)) {
                    m_currentMapping.SetMapping(HumanoidBone::None, i);
                }
                for (int b = 0; b < static_cast<int>(HumanoidBone::Count); ++b) {
                    HumanoidBone boneEnum = static_cast<HumanoidBone>(b);
                    const auto& bInfo = GetHumanoidBoneInfo(boneEnum);
                    bool isSel = (mapped == boneEnum);
                    if (ImGui::Selectable(bInfo.displayName, isSel)) {
                        m_currentMapping.SetMapping(boneEnum, i, 1.0f);
                        if (BoneClassifierAI::Get().IsAutoLearnEnabled()) {
                            BoneClassifierAI::Get().LearnCorrection(joint.name, boneEnum, &skel, i);
                            ShowStatusMessage("AI learned mapping: " + joint.name + " -> " + bInfo.displayName);
                        }
                    }
                    if (isSel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Column 6: Confidence
            ImGui::TableSetColumnIndex(5);
            if (mapped != HumanoidBone::None) {
                float conf = m_currentMapping.GetConfidence(i) * 100.0f;
                ImVec4 cCol = (conf > 75.0f) ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : (conf > 40.0f ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextColored(cCol, "%.1f%%", conf);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "-");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void BoneMappingWindow::DrawTrainingStudioTab() {
    auto& ai = BoneClassifierAI::Get();

    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "AI Machine Learning Studio & Dataset Manager");
    ImGui::Separator();

    // Section 1: Interactive Training
    if (ImGui::CollapsingHeader("Train AI Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool autoLearn = ai.IsAutoLearnEnabled();
        if (ImGui::Checkbox("Live Auto-Learn on manual corrections", &autoLearn)) {
            ai.SetAutoLearnEnabled(autoLearn);
        }

        ImGui::SliderInt("Epochs", &m_trainEpochs, 5, 200);
        ImGui::SliderFloat("Learning Rate", &m_trainLearningRate, 0.01f, 0.30f, "%.3f");

        if (m_activeCharacter) {
            if (ImGui::Button("Add Active Skeleton to Dataset", ImVec2(240, 28))) {
                ai.AddSkeletonToDataset(m_activeCharacter->GetName(), m_activeCharacter->GetSkeleton(), m_currentMapping);
                ShowStatusMessage("Added skeleton '" + m_activeCharacter->GetName() + "' to training dataset!");
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("START TRAINING AI (START TRAINING)", ImVec2(300, 28))) {
            TrainMetrics metrics = ai.TrainModel(m_trainEpochs, m_trainLearningRate);
            ai.SaveModel();
            ShowStatusMessage("AI Trained: Loss " + std::to_string(metrics.initialLoss).substr(0, 5) + " -> " + std::to_string(metrics.finalLoss).substr(0, 5) + ", Accuracy: " + std::to_string(metrics.accuracy).substr(0, 4) + "%");
        }

        const auto& metrics = ai.GetLastTrainMetrics();
        if (metrics.epochsRun > 0) {
            ImGui::Separator();
            ImGui::Text("Training Results:");
            ImGui::Text("Epochs: %d  |  Dataset Samples: %d  |  Learned Vocabulary: %d tokens", metrics.epochsRun, metrics.sampleCount, metrics.vocabularySize);
            ImGui::Text("Initial Loss: %.4f  -->  Final Loss: %.4f", metrics.initialLoss, metrics.finalLoss);
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Classification Accuracy: %.1f%%", metrics.accuracy);
            ImGui::ProgressBar(metrics.accuracy / 100.0f, ImVec2(300, 0), "");
        }
    }

    // Section 2: Custom Pattern Rule Builder
    if (ImGui::CollapsingHeader("Teach AI Specific Pattern Rule", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Define a naming rule: When a bone name contains token [X], map to Humanoid Bone [Y]");

        ImGui::InputText("Token / Keyword", m_customRuleToken, sizeof(m_customRuleToken));

        const char* boneNames[static_cast<int>(HumanoidBone::Count)];
        for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
            boneNames[i] = GetHumanoidBoneInfo(static_cast<HumanoidBone>(i)).displayName;
        }

        ImGui::Combo("Target Humanoid Bone", &m_customRuleTargetBone, boneNames, IM_ARRAYSIZE(boneNames));
        ImGui::SliderFloat("Weight Boost", &m_customRuleWeight, 1.0f, 50.0f, "%.1f");

        if (ImGui::Button("Add Rule & Save to Model")) {
            if (strlen(m_customRuleToken) > 0) {
                ai.LearnRule(m_customRuleToken, static_cast<HumanoidBone>(m_customRuleTargetBone), m_customRuleWeight);
                ShowStatusMessage("Added rule: token '" + std::string(m_customRuleToken) + "' -> " + boneNames[m_customRuleTargetBone]);
            }
        }
    }

    // Section 3: Dataset & Model Management
    if (ImGui::CollapsingHeader("Model Storage & Factory Presets")) {
        ImGui::Text("Total Training Samples in Dataset: %d", (int)ai.GetTrainingDataset().size());
        ImGui::Text("Learned Vocabulary Size: %d unique tokens", (int)ai.GetVocabularySize());

        if (ImGui::Button("Save Model (Config/bone_ai_model.json)")) {
            if (ai.SaveModel()) {
                ShowStatusMessage("Model saved successfully to Config/bone_ai_model.json");
            } else {
                ShowStatusMessage("Failed to save model.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Model from Disk")) {
            if (ai.LoadModel()) {
                ShowStatusMessage("Model reloaded from disk.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to Factory Presets (Mixamo/UE/Unity/VRoid)")) {
            ai.ResetToDefaults();
            ShowStatusMessage("AI reset to factory presets.");
        }
    }
}
#else
void BoneMappingWindow::Draw(Renderer* renderer) {}
#endif

} // namespace EngineAnim
