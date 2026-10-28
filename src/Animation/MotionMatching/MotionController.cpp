#include "MotionController.h"
#include <chrono>
#include <cmath>

namespace EngineAnim {

MotionController::MotionController() {
}

void MotionController::Initialize(const OzzSkeleton* skeleton, SkinnedMesh* mesh, MotionDatabase* database) {
    m_skeleton = skeleton;
    m_mesh = mesh;
    m_database = database;

    if (m_skeleton) {
        int n = m_skeleton->GetNumJoints();
        m_currentPose.resize(n, Transform::Identity());
        m_inOutOffsets.resize(n, Transform::Identity());
        m_inOutOffsetVels.resize(n, Transform(Vec3(0, 0, 0), Quat::Identity(), Vec3(0, 0, 0)));
        m_modelMatrices.resize(n);

        for (int i = 0; i < n; ++i) {
            m_currentPose[i] = m_skeleton->GetJoint(i).restPose;
        }
    }

    m_currentFrameIndex = -1;
    m_searchTimer = 0.0f;
}

void MotionController::BuildQueryFeature(float* rawQuery) {
    if (!m_database || !m_skeleton || m_currentPose.empty()) return;

    // Forward kinematics of current pose
    int n = m_skeleton->GetNumJoints();
    std::vector<Vec3> globalPos(n);
    std::vector<Quat> globalRot(n);

    for (int i = 0; i < n; ++i) {
        int16_t parent = m_skeleton->GetParentIndex(i);
        if (parent < 0 || parent >= i) {
            globalPos[i] = m_currentPose[i].translation;
            globalRot[i] = m_currentPose[i].rotation;
        } else {
            Vec3 pPos = globalPos[parent];
            Quat pRot = globalRot[parent];
            globalPos[i] = pPos + Rotate(pRot, m_currentPose[i].translation);
            globalRot[i] = (pRot * m_currentPose[i].rotation).Normalized();
        }
    }

    int lfIdx = (boneMapping.leftFootJoint >= 0 && boneMapping.leftFootJoint < n) ? boneMapping.leftFootJoint : 0;
    int rfIdx = (boneMapping.rightFootJoint >= 0 && boneMapping.rightFootJoint < n) ? boneMapping.rightFootJoint : 0;
    int hipIdx = (boneMapping.hipsJoint >= 0 && boneMapping.hipsJoint < n) ? boneMapping.hipsJoint : 0;

    Quat rootRotInv = worldRotation.Inverse();
    Vec3 rootPos = worldPosition;

    int idx = 0;

    // 1. Left Foot Position (3)
    Vec3 lfPosRel = Rotate(rootRotInv, globalPos[lfIdx]);
    rawQuery[idx++] = lfPosRel.x;
    rawQuery[idx++] = lfPosRel.y;
    rawQuery[idx++] = lfPosRel.z;

    // 2. Right Foot Position (3)
    Vec3 rfPosRel = Rotate(rootRotInv, globalPos[rfIdx]);
    rawQuery[idx++] = rfPosRel.x;
    rawQuery[idx++] = rfPosRel.y;
    rawQuery[idx++] = rfPosRel.z;

    // 3. Left Foot Velocity (3)
    Vec3 lfVelRel = Rotate(rootRotInv, worldVelocity);
    rawQuery[idx++] = lfVelRel.x;
    rawQuery[idx++] = lfVelRel.y;
    rawQuery[idx++] = lfVelRel.z;

    // 4. Right Foot Velocity (3)
    Vec3 rfVelRel = Rotate(rootRotInv, worldVelocity);
    rawQuery[idx++] = rfVelRel.x;
    rawQuery[idx++] = rfVelRel.y;
    rawQuery[idx++] = rfVelRel.z;

    // 5. Hip Velocity (3)
    Vec3 hipVelRel = Rotate(rootRotInv, worldVelocity);
    rawQuery[idx++] = hipVelRel.x;
    rawQuery[idx++] = hipVelRel.y;
    rawQuery[idx++] = hipVelRel.z;

    // 6. Trajectory Future Positions (6)
    const auto& trajPoints = m_trajectory.GetPoints();
    for (int k = 1; k < MotionTrajectory::TRAJECTORY_POINTS; ++k) {
        Vec3 tPosRel = Rotate(rootRotInv, trajPoints[k].position - rootPos);
        rawQuery[idx++] = tPosRel.x;
        rawQuery[idx++] = tPosRel.z;
    }

    // 7. Trajectory Future Directions (6)
    Vec3 fwd(0, 0, 1);
    for (int k = 1; k < MotionTrajectory::TRAJECTORY_POINTS; ++k) {
        Vec3 tDirRel = Rotate(rootRotInv, Rotate(trajPoints[k].rotation, fwd));
        rawQuery[idx++] = tDirRel.x;
        rawQuery[idx++] = tDirRel.z;
    }
}

void MotionController::Update(
    Vec3 inputDir,
    bool isSprinting,
    bool isCrouching,
    float dt,
    ID3D12Resource* pVertexBuffer
) {
    if (!enabled || !m_skeleton || !m_database || m_database->GetNumFrames() == 0) return;

    float speed = maxWalkSpeed;
    if (isSprinting) speed = maxSprintSpeed;
    else if (isCrouching) speed = maxWalkSpeed * 0.5f;
    else if (inputDir.LengthSq() > 0.5f) speed = maxRunSpeed;

    // 1. Update trajectory prediction
    m_trajectory.Update(worldPosition, worldRotation, worldVelocity, inputDir, speed, 0.25f, dt);
    worldVelocity = m_trajectory.GetVelocity();

    // 2. Perform Motion Matching Search periodically
    m_searchTimer += dt;
    if (m_searchTimer >= searchInterval || m_currentFrameIndex < 0) {
        m_searchTimer = 0.0f;

        float rawQuery[MotionDatabase::FEATURE_DIM];
        BuildQueryFeature(rawQuery);

        float normalizedQuery[MotionDatabase::FEATURE_DIM];
        m_database->NormalizeQuery(rawQuery, normalizedQuery);

        auto startTime = std::chrono::high_resolution_clock::now();
        MotionMatchResult match = MotionMatcher::Search(
            *m_database,
            normalizedQuery,
            m_currentFrameIndex,
            weights.continuityCost
        );
        auto endTime = std::chrono::high_resolution_clock::now();
        m_debugStats.searchTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        if (match.frameIndex >= 0 && match.frameIndex != m_currentFrameIndex) {
            // Trigger inertialization transition
            const auto& dstFrame = m_database->GetFrame(match.frameIndex);
            MotionMatcher::InertializeTransition(m_inOutOffsets, m_inOutOffsetVels, m_currentPose, dstFrame.localTransforms);

            m_debugStats.matchedAnimName = m_database->GetAnimationName(match.animIndex);
            m_debugStats.matchedFrame = match.frameIndex;
            m_debugStats.matchCost = match.cost;

            m_currentFrameIndex = match.frameIndex;
        }
    }

    // 3. Advance currently playing frame
    if (m_currentFrameIndex >= 0 && m_currentFrameIndex < m_database->GetNumFrames()) {
        const auto& targetFrame = m_database->GetFrame(m_currentFrameIndex);
        m_debugStats.currentAnimName = m_database->GetAnimationName(targetFrame.animIndex);
        m_debugStats.currentFrame = m_currentFrameIndex;

        // Apply inertialized blending
        MotionMatcher::InertializeUpdate(
            m_currentPose,
            m_inOutOffsets,
            m_inOutOffsetVels,
            targetFrame.localTransforms,
            transitionDuration,
            dt
        );

        // Root Motion extraction
        if (rootMotionTranslation) {
            Vec3 deltaPos = targetFrame.rootVelocity * dt;
            worldPosition += Rotate(worldRotation, deltaPos);
        }
        if (rootMotionRotation) {
            Quat deltaRot(
                targetFrame.rootAngularVelocity.x * dt * 0.5f,
                targetFrame.rootAngularVelocity.y * dt * 0.5f,
                targetFrame.rootAngularVelocity.z * dt * 0.5f,
                1.0f
            );
            worldRotation = (worldRotation * deltaRot).Normalized();
        }

        // Advance to next frame in current animation range
        const auto& starts = m_database->GetRangeStarts();
        const auto& stops = m_database->GetRangeStops();
        int animIdx = targetFrame.animIndex;
        if (animIdx < static_cast<int>(stops.size())) {
            m_currentFrameIndex++;
            if (m_currentFrameIndex >= stops[animIdx]) {
                m_currentFrameIndex = starts[animIdx]; // Loop range
            }
        }
    }

    // 4. Compute model matrices via LocalToModel job
    OzzLocalToModelJob l2mJob;
    l2mJob.skeleton = m_skeleton;
    l2mJob.inputLocalTransforms = &m_currentPose;
    l2mJob.outputModelMatrices = &m_modelMatrices;
    l2mJob.rootTransform = Transform::Identity();
    l2mJob.Run();

    // 5. Deform skinned mesh and upload to GPU
    if (m_mesh) {
        m_mesh->Deform(m_modelMatrices);
        if (pVertexBuffer) {
            m_mesh->UploadToVertexBuffer(pVertexBuffer);
        }
    }

    // Update debug stats
    m_debugStats.enabled = enabled;
    m_debugStats.totalDatabaseFrames = m_database->GetNumFrames();
    m_debugStats.totalDatabaseAnims = m_database->GetNumAnimations();
    m_debugStats.characterPosition = worldPosition;
    m_debugStats.characterRotation = worldRotation;
}

} // namespace EngineAnim
