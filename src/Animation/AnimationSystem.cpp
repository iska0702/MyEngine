#include "AnimationSystem.h"
#if defined(_WIN32)
#include "BoneMappingWindow.h"
#include "BoneClassifierAI.h"
#include "Renderer/Renderer.h"
#include "Core/Window.h"
#include "Renderer/Camera.h"
#include "Input/DualSense.h"
#include "imgui.h"
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#include <filesystem>
#include <iostream>

namespace EngineAnim {

AnimationSystem& AnimationSystem::Get() {
    static AnimationSystem s_instance;
    return s_instance;
}

void AnimationSystem::Initialize(Renderer* renderer) {
    m_renderer = renderer;
    BoneClassifierAI::Get().Initialize();
    BoneMappingWindow::Get().Initialize(renderer);
}

void AnimationSystem::ImportCharacterDialog(Renderer* renderer) {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "FBX Character Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Import Character (FBX)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    // Set initial directory to src/Content/Characters or src/Content/Animations
    if (std::filesystem::exists("src/Content/Characters")) {
        ofn.lpstrInitialDir = "src\\Content\\Characters";
    } else if (std::filesystem::exists("src/Content/Animations")) {
        ofn.lpstrInitialDir = "src\\Content\\Animations";
    }

    if (GetOpenFileNameA(&ofn)) {
        ImportCharacter(filename, renderer);
    }
}

std::shared_ptr<CharacterAsset> AnimationSystem::ImportCharacter(const std::string& fbxPath, Renderer* renderer) {
    if (!renderer) renderer = m_renderer;
    if (!renderer) return nullptr;

    auto character = CharacterAsset::ImportFromFBX(fbxPath, renderer);
    if (character) {
        m_characters.push_back(character);
        m_selectedCharIndex = static_cast<int>(m_characters.size() - 1);
        BoneMappingWindow::Get().SetActiveCharacter(character);
        LOG("Character imported successfully: " << character->GetName());
    } else {
        MessageBoxA(nullptr, "Failed to import Character FBX. Please verify the file has valid bones and geometry.", "Import Error", MB_OK | MB_ICONERROR);
    }
    return character;
}

std::shared_ptr<CharacterAsset> AnimationSystem::GetSelectedCharacter() const {
    if (m_selectedCharIndex >= 0 && m_selectedCharIndex < static_cast<int>(m_characters.size())) {
        return m_characters[m_selectedCharIndex];
    }
    return nullptr;
}

void AnimationSystem::Update(float dt, Window* window, Camera* camera) {
    if (!window || !camera || !m_renderer) return;

    bool isPlayMode = !window->IsCursorEnabled();

    auto& dualSense = EngineInput::DualSenseController::Get();

    static bool s_animCrouchToggled = false;
    if (dualSense.IsConnected()) {
        if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::R3)) {
            s_animCrouchToggled = !s_animCrouchToggled;
        }
        if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::Cross) || dualSense.IsButtonPressed(EngineInput::DualSenseButton::L3)) {
            s_animCrouchToggled = false;
        }
    }
    if (window->IsKeyDown(VK_SPACE)) {
        s_animCrouchToggled = false;
    }

    bool isCrouching = window->IsKeyDown(VK_CONTROL) || window->IsKeyDown('C') ||
                       (dualSense.IsConnected() && (dualSense.IsButtonDown(EngineInput::DualSenseButton::Circle) ||
                                                    dualSense.IsButtonDown(EngineInput::DualSenseButton::R3) ||
                                                    s_animCrouchToggled));

    static bool s_animSprintToggled = false;
    if (dualSense.IsConnected()) {
        if (dualSense.IsButtonPressed(EngineInput::DualSenseButton::L3)) {
            s_animSprintToggled = !s_animSprintToggled;
            if (s_animSprintToggled) {
                s_animCrouchToggled = false;
            }
        }
    }

    // Compute input movement in camera world space
    Vec3 inputDir(0, 0, 0);

    if (isPlayMode) {
        Vec3 camFwd = camera->GetForwardVector();
        camFwd.y = 0.0f;
        camFwd = camFwd.Normalized();

        Vec3 camRight = camera->GetRightVector();
        camRight.y = 0.0f;
        camRight = camRight.Normalized();

        if (window->IsKeyDown('W')) inputDir += camFwd;
        if (window->IsKeyDown('S')) inputDir -= camFwd;
        if (window->IsKeyDown('D')) inputDir += camRight;
        if (window->IsKeyDown('A')) inputDir -= camRight;

        if (dualSense.IsConnected()) {
            float padLX = dualSense.GetLeftStickX();
            float padLY = dualSense.GetLeftStickY();
            if (std::abs(padLX) > 0.01f || std::abs(padLY) > 0.01f) {
                inputDir += camFwd * padLY + camRight * padLX;
            }
            if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadUp)) inputDir += camFwd;
            if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadDown)) inputDir -= camFwd;
            if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadRight)) inputDir += camRight;
            if (dualSense.IsButtonDown(EngineInput::DualSenseButton::DPadLeft)) inputDir -= camRight;
        }

        if (inputDir.LengthSq() > 1e-4f) {
            inputDir = inputDir.Normalized();
        } else {
            s_animSprintToggled = false;
        }
    }

    if (isCrouching) {
        s_animSprintToggled = false;
    }

    bool isSprinting = (s_animSprintToggled || window->IsKeyDown(VK_SHIFT) || (dualSense.IsConnected() && dualSense.IsButtonDown(EngineInput::DualSenseButton::L3))) && !isCrouching;

    auto& dynModels = m_renderer->GetDynamicModels();

    for (auto& character : m_characters) {
        if (!character) continue;
        if(character->ragdoll.Active()&&(character->renderModelIndex<0||character->renderModelIndex>=(int)dynModels.size()||dynModels[character->renderModelIndex].modelId!=character->ragdoll.ownerModelId))character->ragdoll.Reset();

        ID3D12Resource* pVB = nullptr;
        if (character->renderModelIndex >= 0 && character->renderModelIndex < static_cast<int>(dynModels.size())) {
            auto& dm = dynModels[character->renderModelIndex];
            if(character->ragdoll.Active()){dm.position=character->ragdoll.modelPosition;dm.rotation=character->ragdoll.modelRotation;dm.scale=character->ragdoll.modelScale;}
#if defined(_WIN32)
            pVB = dm.vertexBuffer.Get();
#else
            pVB = (ID3D12Resource*)dm.vertexBuffer;
#endif

            // Synchronize model position from Motion Matching controller
            if (!character->ragdoll.Active() && character->motionMatchingEnabled && !character->IsPreviewing() && character->GetMotionDatabase().GetNumFrames() > 0) {
                const auto& mc = character->GetMotionController();
                dm.position = mc.worldPosition.ToXM();

                // Compute yaw angle from quaternion
                Quat rot = mc.worldRotation;
                float yaw = std::atan2(2.0f * (rot.w * rot.y + rot.x * rot.z), 1.0f - 2.0f * (rot.y * rot.y + rot.z * rot.z));
                dm.rotation.y = DirectX::XMConvertToDegrees(yaw);
            }
        }

        character->Update(dt, inputDir, isSprinting, isCrouching, isPlayMode, pVB);
    }
}

std::shared_ptr<CharacterAsset> AnimationSystem::FindCharacterForModel(Renderer* renderer,uint32_t modelId) const {
    if(!renderer)return nullptr;
    const auto& models=renderer->GetDynamicModels();
    for(auto& character:m_characters)if(character&&character->renderModelIndex>=0&&character->renderModelIndex<(int)models.size()&&models[character->renderModelIndex].modelId==modelId)return character;
    return nullptr;
}
bool AnimationSystem::SetRagdollEnabled(Renderer* renderer,uint32_t modelId,bool enabled){
    auto character=FindCharacterForModel(renderer,modelId);if(!character)return false;
    auto& model=renderer->GetDynamicModels()[character->renderModelIndex];auto& physics=EnginePhysics::PhysicsSystem::Get();
    if(!enabled){character->ragdoll.Reset();physics.ApplyModelPhysics(renderer,modelId,true);return true;}
    if(character->ragdoll.Active())return true;
    auto world=DirectX::XMMatrixScaling(model.scale.x,model.scale.y,model.scale.z)*DirectX::XMMatrixRotationRollPitchYaw(DirectX::XMConvertToRadians(model.rotation.x),DirectX::XMConvertToRadians(model.rotation.y),DirectX::XMConvertToRadians(model.rotation.z))*DirectX::XMMatrixTranslation(model.position.x,model.position.y,model.position.z);
    physics.RemoveActorForModel(modelId);
    if(!character->ragdoll.Start(character->GetSkeleton(),character->GetHumanoidBoneMapping(),character->CurrentPose(),world,model.ragdollSettings)){
        physics.ApplyModelPhysics(renderer,modelId,true);return false;
    }
    character->ragdoll.ownerModelId=modelId;character->ragdoll.modelPosition=model.position;character->ragdoll.modelRotation=model.rotation;character->ragdoll.modelScale=model.scale;
    return true;
}
bool AnimationSystem::DrawRagdollInspector(Renderer* renderer,uint32_t modelId){
#if !defined(ENGINE_RELEASE)
    if(!ImGui::CollapsingHeader("Ragdoll",ImGuiTreeNodeFlags_DefaultOpen))return false;
    auto character=FindCharacterForModel(renderer,modelId);
    if(!character){ImGui::BeginDisabled();ImGui::Button("Enable ragdoll physics");ImGui::EndDisabled();ImGui::TextWrapped("Ragdoll requires an imported skeletal character. Use Collider & Rigidbody for ordinary objects.");return false;}
    auto& model=renderer->GetDynamicModels()[character->renderModelIndex];auto& settings=model.ragdollSettings;
    bool active=character->ragdoll.Active();
    if(ImGui::Button(active?"Disable ragdoll physics":"Enable ragdoll physics"))SetRagdollEnabled(renderer,modelId,!active);
    ImGui::TextWrapped("TAB starts simulation. While ragdoll is enabled it replaces this mesh's Rigidbody; disabling it restores the Rigidbody.");
    bool changed=false,inherit=settings.useProjectDefaults;
    if(ImGui::Checkbox("Use project ragdoll defaults",&inherit)){
        settings=Ragdoll::EffectiveSettings(settings);settings.useProjectDefaults=inherit;changed=true;
    }
    auto effective=Ragdoll::EffectiveSettings(settings);auto& values=settings.useProjectDefaults?effective:settings;
    ImGui::BeginDisabled(settings.useProjectDefaults);
    changed|=ImGui::DragFloat("Ragdoll mass (kg)",&values.mass,.5f,1,1000,"%.1f",ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::DragFloat("Bone radius (m)",&values.radius,.005f,.01f,.5f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::DragFloat("Ragdoll angular damping",&values.angularDamping,.02f,0,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::SliderFloat("Joint swing / twist limit",&values.jointLimit,5,120,"%.0f deg");
    ImGui::EndDisabled();
    changed|=ImGui::DragFloat("Ragdoll linear damping",&settings.linearDamping,.02f,0,20,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    changed|=ImGui::Checkbox("Ragdoll gravity",&settings.gravity);
    changed|=ImGui::Checkbox("Ragdoll continuous collision",&settings.ccd);
    if(changed){settings.Sanitize();if(character->ragdoll.Active())character->ragdoll.ApplySettings(settings);}
    if(character->ragdoll.Active()){
        static DirectX::XMFLOAT3 impulse={0,25,0};ImGui::DragFloat3("Ragdoll impulse (N s)",&impulse.x,1);
        if(ImGui::Button("Apply ragdoll impulse"))character->ragdoll.Impulse(impulse);
        ImGui::SameLine();if(ImGui::Button("Step physics"))EnginePhysics::PhysicsSystem::Get().SingleStep();
    }
    ImGui::TextWrapped("%s",character->ragdoll.status.c_str());
    return changed;
#else
    return false;
#endif
}

void AnimationSystem::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!renderer) renderer = m_renderer;

    // 1. Character Menu Bar / Import Window
    ImGui::Begin("Character System", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::Button("Character -> Import Character (FBX)")) {
        ImportCharacterDialog(renderer);
    }
    ImGui::SameLine();
    if (ImGui::Button("AI Bone Auto-Mapper & Trainer")) {
        BoneMappingWindow::Get().ToggleOpen();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "|  Characters: %d", (int)m_characters.size());
    ImGui::End();

    // Draw AI Bone Mapping & Training Window
    BoneMappingWindow::Get().Draw(renderer);

    auto character = GetSelectedCharacter();

    // 2. Scene Hierarchy with Skeleton & Bones
    if (ImGui::Begin("Scene Hierarchy")) {
        if (m_characters.empty()) {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No Characters in Scene. Click 'Import Character' to load one.");
        } else {
            for (size_t i = 0; i < m_characters.size(); ++i) {
                auto& c = m_characters[i];
                ImGui::PushID(static_cast<int>(i));
                bool isSelected = (m_selectedCharIndex == static_cast<int>(i));

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                bool nodeOpen = ImGui::TreeNodeEx(c->GetName().c_str(), flags);
                if (ImGui::IsItemClicked()) {
                    m_selectedCharIndex = static_cast<int>(i);
                }

                if (nodeOpen) {
                    DrawCharacterHierarchy(c);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();

    // 3. Animation & Motion Matching Inspector
    if (character) {
        DrawAnimationInspector(character);
        DrawMotionMatchingDebug(character);
        DrawTrajectoryInWorld(character, camera);
    }
}

void AnimationSystem::DrawCharacterHierarchy(std::shared_ptr<CharacterAsset> character) {
    if (!character) return;

    if (ImGui::TreeNode("Mesh")) {
        ImGui::Text("Vertices: %d", (int)character->GetMesh().GetVertexCount());
        ImGui::Text("Triangles: %d", (int)character->GetMesh().GetIndexCount() / 3);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Skeleton")) {
        const auto& skel = character->GetSkeleton();
        ImGui::Text("Joints Count: %d", skel.GetNumJoints());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in AI Bone Mapper")) {
            BoneMappingWindow::Get().SetActiveCharacter(character);
            BoneMappingWindow::Get().SetOpen(true);
        }
        ImGui::Separator();

        // Draw root bones
        for (int i = 0; i < skel.GetNumJoints(); ++i) {
            if (skel.GetParentIndex(i) < 0) {
                DrawBoneTree(skel, i);
            }
        }
        ImGui::TreePop();
    }
}

void AnimationSystem::DrawBoneTree(const OzzSkeleton& skeleton, int boneIdx) {
    if (boneIdx < 0 || boneIdx >= skeleton.GetNumJoints()) return;

    const auto& joint = skeleton.GetJoint(boneIdx);
    bool isSelected = (m_selectedBoneIndex == boneIdx);

    // Find children
    std::vector<int> children;
    for (int i = 0; i < skeleton.GetNumJoints(); ++i) {
        if (skeleton.GetParentIndex(i) == boneIdx) {
            children.push_back(i);
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    std::string nodeLabel = joint.name;
    auto character = GetSelectedCharacter();
    if (character) {
        HumanoidBone hBone = character->GetHumanoidBoneMapping().GetHumanoidBone(boneIdx);
        if (hBone != HumanoidBone::None) {
            float conf = character->GetHumanoidBoneMapping().GetConfidence(boneIdx) * 100.0f;
            nodeLabel += "  [" + std::string(GetHumanoidBoneInfo(hBone).displayName) + " " + std::to_string((int)conf) + "%]";
        }
    }

    bool open = ImGui::TreeNodeEx((void*)(intptr_t)boneIdx, flags, "%s", nodeLabel.c_str());
    if (ImGui::IsItemClicked()) {
        m_selectedBoneIndex = boneIdx;
    }

    if (open) {
        for (int childIdx : children) {
            DrawBoneTree(skeleton, childIdx);
        }
        ImGui::TreePop();
    }
}

void AnimationSystem::DrawAnimationInspector(std::shared_ptr<CharacterAsset> character) {
    if (!character) return;

    ImGui::SetNextWindowSize(ImVec2(550, 650), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animation & Motion Matching Inspector")) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Character: %s", character->GetName().c_str());
        ImGui::Separator();

        if(character->renderModelIndex>=0&&character->renderModelIndex<(int)m_renderer->GetDynamicModels().size())DrawRagdollInspector(m_renderer,m_renderer->GetDynamicModels()[character->renderModelIndex].modelId);

        // Section 1: Character Transform & Details
        if (ImGui::CollapsingHeader("Character Details", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Skeleton Joints: %d", character->GetSkeleton().GetNumJoints());
            ImGui::Text("Mesh Vertices: %d", (int)character->GetMesh().GetVertexCount());

            auto& mc = character->GetMotionController();
            float pos[3] = { mc.worldPosition.x, mc.worldPosition.y, mc.worldPosition.z };
            if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                mc.worldPosition = Vec3(pos[0], pos[1], pos[2]);
            }
        }

        // Section 2: Animation & Mode Config
        if (ImGui::CollapsingHeader("Animation Configuration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Animation System: [ Ozz Animation ]");
            ImGui::Text("Mode:             [ Motion Matching ]");
            ImGui::Checkbox("Motion Matching: Enabled", &character->motionMatchingEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Auto Build Database", &character->autoBuild);

            if (ImGui::Button("BUILD MOTION MATCHING", ImVec2(240, 30))) {
                if (character->BuildMotionMatching()) {
                    MessageBoxA(nullptr, "Motion Matching Database built successfully!", "Build Success", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxA(nullptr, "Build failed. Please assign at least one valid FBX animation.", "Build Warning", MB_OK | MB_ICONWARNING);
                }
            }

            const auto& db = character->GetMotionDatabase();
            if (db.GetNumFrames() > 0) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Database: READY (%d animations, %d frames)", db.GetNumAnimations(), db.GetNumFrames());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Database: Not built yet");
            }
        }

        // Section 3: Animation Preview Controls
        if (character->IsPreviewing()) {
            if (ImGui::CollapsingHeader("Animation Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
                int slotIdx = character->GetPreviewSlotIndex();
                auto& slots = character->GetAnimationSlots();
                if (slotIdx >= 0 && slotIdx < static_cast<int>(slots.size())) {
                    ImGui::Text("Previewing: %s (%s)", slots[slotIdx].name.c_str(), slots[slotIdx].category.c_str());
                }

                if (ImGui::Button(" Resume")) character->PreviewPlay(slotIdx);
                ImGui::SameLine();
                if (ImGui::Button(" Pause")) character->PreviewPause();
                ImGui::SameLine();
                if (ImGui::Button(" Stop")) character->PreviewStop();

                float pTime = character->GetPreviewTime();
                float pDur = character->GetPreviewDuration();
                if (ImGui::SliderFloat("Timeline", &pTime, 0.0f, pDur, "%.2f sec")) {
                    character->SetPreviewTime(pTime);
                }
                ImGui::SliderFloat("Playback Speed", &character->previewSpeed, 0.1f, 3.0f, "%.2fx");
            }
        }

        // Section 4: Motion Matching Animation Set
        if (ImGui::CollapsingHeader("Motion Matching Animations", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& slots = character->GetAnimationSlots();
            std::string currentCategory = "";

            for (size_t i = 0; i < slots.size(); ++i) {
                auto& slot = slots[i];

                if (slot.category != currentCategory) {
                    currentCategory = slot.category;
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", currentCategory.c_str());
                    ImGui::Separator();
                }

                ImGui::PushID(static_cast<int>(i));

                ImGui::Text("%-22s", slot.name.c_str());
                ImGui::SameLine();

                if (ImGui::Button("Select FBX")) {
                    char filename[MAX_PATH] = "";
                    OPENFILENAMEA ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.lpstrFilter = "FBX Animation Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrTitle = "Select Animation FBX";
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                    if (std::filesystem::exists("src/Content/Animations")) {
                        ofn.lpstrInitialDir = "src\\Content\\Animations";
                    }

                    if (GetOpenFileNameA(&ofn)) {
                        character->AssignFBXAnimation(static_cast<int>(i), filename);
                    }
                }

                ImGui::SameLine();
                if (slot.status == "Ready") {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[ %s ]", std::filesystem::path(slot.fbxPath).filename().string().c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Preview")) {
                        character->PreviewPlay(static_cast<int>(i));
                    }
                } else if (slot.status.find("Error") != std::string::npos) {
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[ %s ]", slot.status.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[ None ]");
                }

                if (slot.isCustom) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete")) {
                        character->RemoveCustomAnimation(static_cast<int>(i));
                        ImGui::PopID();
                        break;
                    }
                }

                ImGui::PopID();
            }

            ImGui::Separator();
            static char customCat[64] = "CUSTOM";
            static char customName[64] = "Custom Animation";
            ImGui::InputText("Category", customCat, sizeof(customCat));
            ImGui::InputText("Anim Name", customName, sizeof(customName));
            if (ImGui::Button("+ Add Animation")) {
                character->AddCustomAnimation(customCat, customName, "");
            }
        }

        // Section 5: Motion Matching Settings
        if (ImGui::CollapsingHeader("Motion Matching Settings")) {
            auto& mc = character->GetMotionController();
            ImGui::DragFloat("Trajectory Position Weight", &mc.weights.trajectoryPosition, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Trajectory Direction Weight", &mc.weights.trajectoryDirection, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Pose Position Weight", &mc.weights.footPosition, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Pose Velocity Weight", &mc.weights.footVelocity, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Continuity Weight", &mc.weights.continuityCost, 0.05f, 0.0f, 10.0f);

            ImGui::Separator();
            ImGui::SliderFloat("Search Interval", &mc.searchInterval, 0.05f, 0.50f, "%.2f sec");
            ImGui::SliderFloat("Transition Duration", &mc.transitionDuration, 0.05f, 0.50f, "%.2f sec");

            ImGui::Separator();
            ImGui::Checkbox("Root Motion: Translation", &mc.rootMotionTranslation);
            ImGui::Checkbox("Root Motion: Rotation", &mc.rootMotionRotation);
        }
    }
    ImGui::End();
#endif
}

void AnimationSystem::DrawMotionMatchingDebug(std::shared_ptr<CharacterAsset> character) {
#ifndef ENGINE_RELEASE
    if (!character || !character->motionMatchingEnabled) return;

    const auto& stats = character->GetMotionController().GetDebugStats();

    ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Motion Matching Debug")) {
        ImGui::Text("Enabled:   YES");
        ImGui::Separator();
        ImGui::Text("Current:   %s", stats.currentAnimName.c_str());
        ImGui::Text("Frame:     %d", stats.currentFrame);
        ImGui::Separator();
        ImGui::Text("Matched:   %s", stats.matchedAnimName.c_str());
        ImGui::Text("Frame:     %d", stats.matchedFrame);
        ImGui::Text("Cost:      %.4f", stats.matchCost);
        ImGui::Separator();
        ImGui::Text("Database:  %d anims, %d frames", stats.totalDatabaseAnims, stats.totalDatabaseFrames);
        ImGui::Text("Search:    %.3f ms", stats.searchTimeMs);
        ImGui::Text("Blend:     %.2f sec", stats.blendDuration);
    }
    ImGui::End();
#endif
}

void AnimationSystem::DrawTrajectoryInWorld(std::shared_ptr<CharacterAsset> character, Camera* camera) {
    if (!character || !camera) return;
    // Trajectory debug visualization points can be read from character->GetMotionController().GetTrajectory()
}

bool AnimationSystem::IsBoneMappingWindowOpen() const {
    return BoneMappingWindow::Get().IsOpen();
}

void AnimationSystem::SetBoneMappingWindowOpen(bool open) {
    BoneMappingWindow::Get().SetOpen(open);
}

void AnimationSystem::ToggleBoneMappingWindow() {
    BoneMappingWindow::Get().ToggleOpen();
}

} // namespace EngineAnim
#endif
