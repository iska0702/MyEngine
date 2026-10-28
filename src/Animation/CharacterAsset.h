#pragma once
#include "AnimationCommon.h"
#include "SkinnedMesh.h"
#include "Ozz/OzzRuntime.h"
#include "MotionMatching/MotionDatabase.h"
#include "MotionMatching/MotionController.h"
#include "BoneClassifierAI.h"
#include <string>
#include <vector>
#include <memory>
#include "Physics/Ragdoll.h"

class Renderer;

namespace EngineAnim {

struct AnimationSlot {
    std::string category;
    std::string name;
    std::string fbxPath;
    std::string ozzPath;
    std::string status = "Not Assigned"; // "Not Assigned", "Ready", "Error: ..."
    OzzAnimation ozzAnimation;
    bool isCustom = false;
};

class CharacterAsset {
public:
    CharacterAsset();

    static std::shared_ptr<CharacterAsset> ImportFromFBX(const std::string& fbxPath, Renderer* renderer);

    bool AssignFBXAnimation(int slotIndex, const std::string& fbxPath);
    bool AddCustomAnimation(const std::string& category, const std::string& name, const std::string& fbxPath);
    void RemoveCustomAnimation(int slotIndex);

    bool BuildMotionMatching();
    void RunBoneRecognition();

    void Update(float dt, Vec3 inputDir, bool isSprinting, bool isCrouching, bool isPlayMode, ID3D12Resource* pVertexBuffer);

    // Preview Controls
    void PreviewPlay(int slotIndex);
    void PreviewPause();
    void PreviewStop();
    bool IsPreviewing() const { return m_isPreviewPlaying; }
    int GetPreviewSlotIndex() const { return m_previewSlotIndex; }
    float GetPreviewTime() const { return m_previewTime; }
    void SetPreviewTime(float t) { m_previewTime = t; }
    float GetPreviewDuration() const;
    float previewSpeed = 1.0f;

    // Accessors
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    const OzzSkeleton& GetSkeleton() const { return m_skeleton; }
    OzzSkeleton& GetSkeleton() { return m_skeleton; }

    const SkinnedMesh& GetMesh() const { return m_mesh; }
    SkinnedMesh& GetMesh() { return m_mesh; }

    const MotionDatabase& GetMotionDatabase() const { return m_motionDatabase; }
    MotionDatabase& GetMotionDatabase() { return m_motionDatabase; }

    const MotionController& GetMotionController() const { return m_controller; }
    MotionController& GetMotionController() { return m_controller; }

    const HumanoidBoneMapping& GetHumanoidBoneMapping() const { return m_humanoidBoneMapping; }
    HumanoidBoneMapping& GetHumanoidBoneMapping() { return m_humanoidBoneMapping; }

    std::vector<AnimationSlot>& GetAnimationSlots() { return m_slots; }
    const std::vector<AnimationSlot>& GetAnimationSlots() const { return m_slots; }

    // Settings
    bool autoBuild = true;
    bool motionMatchingEnabled = true;
    int renderModelIndex = -1; // Index in dynamic models list
#if defined(_WIN32)
    Ragdoll ragdoll;
    const std::vector<Matrix4x4>& CurrentPose() const {
        if(m_isPreviewPlaying&&!m_previewModelMatrices.empty())return m_previewModelMatrices;
        if(motionMatchingEnabled&&!m_controller.GetModelMatrices().empty())return m_controller.GetModelMatrices();
        return m_skeleton.GetBindPoseModelMatrices();
    }
#endif

private:
    std::string m_name = "Character";
    std::string m_sourceFbxPath;

    OzzSkeleton m_skeleton;
    SkinnedMesh m_mesh;
    MotionDatabase m_motionDatabase;
    MotionController m_controller;
    HumanoidBoneMapping m_humanoidBoneMapping;

    std::vector<AnimationSlot> m_slots;

    // Preview state
    bool m_isPreviewPlaying = false;
    int m_previewSlotIndex = -1;
    float m_previewTime = 0.0f;
    std::vector<Transform> m_previewPose;
    std::vector<Matrix4x4> m_previewModelMatrices;

    void InitializeDefaultSlots();
};

} // namespace EngineAnim

