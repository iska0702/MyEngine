#pragma once
#include "MotionDatabase.h"
#include "MotionTrajectory.h"
#include "MotionMatcher.h"
#include "../Ozz/OzzRuntime.h"
#include "../SkinnedMesh.h"
#include <memory>
#include <string>

namespace EngineAnim {

struct MotionDebugStats {
    bool enabled = true;
    std::string currentAnimName = "None";
    int currentFrame = 0;
    std::string matchedAnimName = "None";
    int matchedFrame = 0;
    float matchCost = 0.0f;
    int totalDatabaseFrames = 0;
    int totalDatabaseAnims = 0;
    float searchTimeMs = 0.0f;
    float blendDuration = 0.15f;
    Vec3 characterPosition;
    Quat characterRotation;
};

class MotionController {
public:
    MotionController();

    void Initialize(const OzzSkeleton* skeleton, SkinnedMesh* mesh, MotionDatabase* database);

    void Update(
        Vec3 inputDir, // WASD in camera space
        bool isSprinting,
        bool isCrouching,
        float dt,
        ID3D12Resource* pVertexBuffer
    );

    // Settings
    bool enabled = true;
    bool rootMotionTranslation = true;
    bool rootMotionRotation = true;
    float maxWalkSpeed = 2.5f;
    float maxRunSpeed = 5.0f;
    float maxSprintSpeed = 8.0f;
    float searchInterval = 0.10f; // seconds
    float transitionDuration = 0.15f; // seconds

    MotionWeights weights;
    MotionBoneMapping boneMapping;

    // World transform
    Vec3 worldPosition = Vec3(0, 0, 0);
    Quat worldRotation = Quat::Identity();
    Vec3 worldVelocity = Vec3(0, 0, 0);

    const MotionTrajectory& GetTrajectory() const { return m_trajectory; }
    const MotionDebugStats& GetDebugStats() const { return m_debugStats; }
    const std::vector<Matrix4x4>& GetModelMatrices() const { return m_modelMatrices; }

private:
    const OzzSkeleton* m_skeleton = nullptr;
    SkinnedMesh* m_mesh = nullptr;
    MotionDatabase* m_database = nullptr;

    MotionTrajectory m_trajectory;
    MotionDebugStats m_debugStats;

    int m_currentFrameIndex = -1;
    float m_searchTimer = 0.0f;

    // Inertialization state
    std::vector<Transform> m_currentPose;
    std::vector<Transform> m_inOutOffsets;
    std::vector<Transform> m_inOutOffsetVels;
    std::vector<Matrix4x4> m_modelMatrices;

    void BuildQueryFeature(float* rawQuery);
};

} // namespace EngineAnim
