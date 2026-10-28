#pragma once
#include "../AnimationCommon.h"
#include <vector>
#include <string>

namespace EngineAnim {

enum MotionBoneIndex {
    MBone_Root = 0,
    MBone_Hips,
    MBone_LeftUpLeg,
    MBone_LeftLeg,
    MBone_LeftFoot,
    MBone_RightUpLeg,
    MBone_RightLeg,
    MBone_RightFoot,
    MBone_Spine,
    MBone_Chest,
    MBone_Neck,
    MBone_Head,
    MBone_LeftArm,
    MBone_LeftForeArm,
    MBone_LeftHand,
    MBone_RightArm,
    MBone_RightForeArm,
    MBone_RightHand,
    MBone_Count
};

struct MotionBoneMapping {
    int rootJoint = 0;
    int hipsJoint = 1;
    int leftFootJoint = 4;
    int rightFootJoint = 8;
    int leftHandJoint = 18;
    int rightHandJoint = 22;
    int headJoint = 14;
    int chestJoint = 11;
};

struct MotionFrameData {
    int animIndex = 0;
    float time = 0.0f;
    
    // World / character space root transform
    Vec3 rootPosition;
    Quat rootRotation;
    Vec3 rootVelocity;
    Vec3 rootAngularVelocity;

    // Joint local transforms for this frame
    std::vector<Transform> localTransforms;
};

} // namespace EngineAnim
