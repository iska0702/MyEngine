#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace EngineAnim {

// Standard Humanoid Bone Enumeration (Compatible with Unity Humanoid, Unreal Mannequin, Mixamo, VRoid)
enum class HumanoidBone : int16_t {
    None = -1,

    // Root & Pelvis
    Root = 0,
    Hips,

    // Spine & Torso
    Spine,
    Spine1,
    Spine2,
    Chest,
    UpperChest,

    // Head & Face
    Neck,
    Head,
    LeftEye,
    RightEye,
    Jaw,

    // Left Arm
    LeftShoulder,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,

    // Right Arm
    RightShoulder,
    RightUpperArm,
    RightLowerArm,
    RightHand,

    // Left Leg
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    LeftToes,

    // Right Leg
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    RightToes,

    // Left Hand Fingers
    LeftThumbProximal,
    LeftThumbIntermediate,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,

    // Right Hand Fingers
    RightThumbProximal,
    RightThumbIntermediate,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,

    // Auxiliary / Twist / IK
    LeftUpperArmTwist,
    LeftLowerArmTwist,
    RightUpperArmTwist,
    RightLowerArmTwist,
    LeftUpperLegTwist,
    LeftLowerLegTwist,
    RightUpperLegTwist,
    RightLowerLegTwist,

    Count
};

enum class BoneCategory : uint8_t {
    RootPelvis = 0,
    SpineTorso,
    HeadFace,
    LeftArm,
    RightArm,
    LeftLeg,
    RightLeg,
    LeftFingers,
    RightFingers,
    AuxiliaryTwist,
    Count
};

enum class BoneSide : uint8_t {
    Center = 0,
    Left,
    Right
};

struct HumanoidBoneInfo {
    HumanoidBone bone;
    const char* idName;
    const char* displayName;
    const char* displayNameRu;
    BoneCategory category;
    BoneSide side;
    HumanoidBone defaultParent;
    HumanoidBone mirrorBone;
    bool isRequired;
    float defaultRelativeY; // Expected relative vertical position 0.0 (feet) to 1.0 (head)
};

inline const HumanoidBoneInfo& GetHumanoidBoneInfo(HumanoidBone bone) {
    static const HumanoidBoneInfo s_boneInfoTable[static_cast<size_t>(HumanoidBone::Count)] = {
        // Root & Pelvis
        { HumanoidBone::Root, "Root", "Root", "Root", BoneCategory::RootPelvis, BoneSide::Center, HumanoidBone::None, HumanoidBone::None, false, 0.0f },
        { HumanoidBone::Hips, "Hips", "Hips / Pelvis", "Hips / Pelvis", BoneCategory::RootPelvis, BoneSide::Center, HumanoidBone::Root, HumanoidBone::None, true, 0.55f },

        // Spine & Torso
        { HumanoidBone::Spine, "Spine", "Spine", "Spine", BoneCategory::SpineTorso, BoneSide::Center, HumanoidBone::Hips, HumanoidBone::None, true, 0.60f },
        { HumanoidBone::Spine1, "Spine1", "Spine 1", "Spine 1", BoneCategory::SpineTorso, BoneSide::Center, HumanoidBone::Spine, HumanoidBone::None, false, 0.65f },
        { HumanoidBone::Spine2, "Spine2", "Spine 2", "Spine 2", BoneCategory::SpineTorso, BoneSide::Center, HumanoidBone::Spine1, HumanoidBone::None, false, 0.70f },
        { HumanoidBone::Chest, "Chest", "Chest", "Chest", BoneCategory::SpineTorso, BoneSide::Center, HumanoidBone::Spine, HumanoidBone::None, true, 0.72f },
        { HumanoidBone::UpperChest, "UpperChest", "Upper Chest", "Upper Chest", BoneCategory::SpineTorso, BoneSide::Center, HumanoidBone::Chest, HumanoidBone::None, false, 0.78f },

        // Head & Face
        { HumanoidBone::Neck, "Neck", "Neck", "Neck", BoneCategory::HeadFace, BoneSide::Center, HumanoidBone::Chest, HumanoidBone::None, false, 0.84f },
        { HumanoidBone::Head, "Head", "Head", "Head", BoneCategory::HeadFace, BoneSide::Center, HumanoidBone::Neck, HumanoidBone::None, true, 0.92f },
        { HumanoidBone::LeftEye, "LeftEye", "Left Eye", "Left Eye", BoneCategory::HeadFace, BoneSide::Left, HumanoidBone::Head, HumanoidBone::RightEye, false, 0.93f },
        { HumanoidBone::RightEye, "RightEye", "Right Eye", "Right Eye", BoneCategory::HeadFace, BoneSide::Right, HumanoidBone::Head, HumanoidBone::LeftEye, false, 0.93f },
        { HumanoidBone::Jaw, "Jaw", "Jaw", "Jaw", BoneCategory::HeadFace, BoneSide::Center, HumanoidBone::Head, HumanoidBone::None, false, 0.88f },

        // Left Arm
        { HumanoidBone::LeftShoulder, "LeftShoulder", "Left Shoulder / Clavicle", "Left Shoulder / Clavicle", BoneCategory::LeftArm, BoneSide::Left, HumanoidBone::Chest, HumanoidBone::RightShoulder, false, 0.80f },
        { HumanoidBone::LeftUpperArm, "LeftUpperArm", "Left Upper Arm", "Left Upper Arm", BoneCategory::LeftArm, BoneSide::Left, HumanoidBone::LeftShoulder, HumanoidBone::RightUpperArm, true, 0.78f },
        { HumanoidBone::LeftLowerArm, "LeftLowerArm", "Left Lower Arm / Forearm", "Left Lower Arm / Forearm", BoneCategory::LeftArm, BoneSide::Left, HumanoidBone::LeftUpperArm, HumanoidBone::RightLowerArm, true, 0.60f },
        { HumanoidBone::LeftHand, "LeftHand", "Left Hand / Wrist", "Left Hand / Wrist", BoneCategory::LeftArm, BoneSide::Left, HumanoidBone::LeftLowerArm, HumanoidBone::RightHand, true, 0.48f },

        // Right Arm
        { HumanoidBone::RightShoulder, "RightShoulder", "Right Shoulder / Clavicle", "Right Shoulder / Clavicle", BoneCategory::RightArm, BoneSide::Right, HumanoidBone::Chest, HumanoidBone::LeftShoulder, false, 0.80f },
        { HumanoidBone::RightUpperArm, "RightUpperArm", "Right Upper Arm", "Right Upper Arm", BoneCategory::RightArm, BoneSide::Right, HumanoidBone::RightShoulder, HumanoidBone::LeftUpperArm, true, 0.78f },
        { HumanoidBone::RightLowerArm, "RightLowerArm", "Right Lower Arm / Forearm", "Right Lower Arm / Forearm", BoneCategory::RightArm, BoneSide::Right, HumanoidBone::RightUpperArm, HumanoidBone::LeftLowerArm, true, 0.60f },
        { HumanoidBone::RightHand, "RightHand", "Right Hand / Wrist", "Right Hand / Wrist", BoneCategory::RightArm, BoneSide::Right, HumanoidBone::RightLowerArm, HumanoidBone::LeftHand, true, 0.48f },

        // Left Leg
        { HumanoidBone::LeftUpperLeg, "LeftUpperLeg", "Left Upper Leg / Thigh", "Left Upper Leg / Thigh", BoneCategory::LeftLeg, BoneSide::Left, HumanoidBone::Hips, HumanoidBone::RightUpperLeg, true, 0.50f },
        { HumanoidBone::LeftLowerLeg, "LeftLowerLeg", "Left Lower Leg / Calf", "Left Lower Leg / Calf", BoneCategory::LeftLeg, BoneSide::Left, HumanoidBone::LeftUpperLeg, HumanoidBone::RightLowerLeg, true, 0.28f },
        { HumanoidBone::LeftFoot, "LeftFoot", "Left Foot / Ankle", "Left Foot / Ankle", BoneCategory::LeftLeg, BoneSide::Left, HumanoidBone::LeftLowerLeg, HumanoidBone::RightFoot, true, 0.08f },
        { HumanoidBone::LeftToes, "LeftToes", "Left Toes", "Left Toes", BoneCategory::LeftLeg, BoneSide::Left, HumanoidBone::LeftFoot, HumanoidBone::RightToes, false, 0.02f },

        // Right Leg
        { HumanoidBone::RightUpperLeg, "RightUpperLeg", "Right Upper Leg / Thigh", "Right Upper Leg / Thigh", BoneCategory::RightLeg, BoneSide::Right, HumanoidBone::Hips, HumanoidBone::LeftUpperLeg, true, 0.50f },
        { HumanoidBone::RightLowerLeg, "RightLowerLeg", "Right Lower Leg / Calf", "Right Lower Leg / Calf", BoneCategory::RightLeg, BoneSide::Right, HumanoidBone::RightUpperLeg, HumanoidBone::LeftLowerLeg, true, 0.28f },
        { HumanoidBone::RightFoot, "RightFoot", "Right Foot / Ankle", "Right Foot / Ankle", BoneCategory::RightLeg, BoneSide::Right, HumanoidBone::RightLowerLeg, HumanoidBone::LeftFoot, true, 0.08f },
        { HumanoidBone::RightToes, "RightToes", "Right Toes", "Right Toes", BoneCategory::RightLeg, BoneSide::Right, HumanoidBone::RightFoot, HumanoidBone::LeftToes, false, 0.02f },

        // Left Fingers
        { HumanoidBone::LeftThumbProximal, "LeftThumbProximal", "Left Thumb Proximal", "Left Thumb Proximal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftHand, HumanoidBone::RightThumbProximal, false, 0.46f },
        { HumanoidBone::LeftThumbIntermediate, "LeftThumbIntermediate", "Left Thumb Intermediate", "Left Thumb Intermediate", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftThumbProximal, HumanoidBone::RightThumbIntermediate, false, 0.45f },
        { HumanoidBone::LeftThumbDistal, "LeftThumbDistal", "Left Thumb Distal", "Left Thumb Distal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftThumbIntermediate, HumanoidBone::RightThumbDistal, false, 0.44f },

        { HumanoidBone::LeftIndexProximal, "LeftIndexProximal", "Left Index Proximal", "Left Index Proximal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftHand, HumanoidBone::RightIndexProximal, false, 0.46f },
        { HumanoidBone::LeftIndexIntermediate, "LeftIndexIntermediate", "Left Index Intermediate", "Left Index Intermediate", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftIndexProximal, HumanoidBone::RightIndexIntermediate, false, 0.44f },
        { HumanoidBone::LeftIndexDistal, "LeftIndexDistal", "Left Index Distal", "Left Index Distal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftIndexIntermediate, HumanoidBone::RightIndexDistal, false, 0.42f },

        { HumanoidBone::LeftMiddleProximal, "LeftMiddleProximal", "Left Middle Proximal", "Left Middle Proximal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftHand, HumanoidBone::RightMiddleProximal, false, 0.46f },
        { HumanoidBone::LeftMiddleIntermediate, "LeftMiddleIntermediate", "Left Middle Intermediate", "Left Middle Intermediate", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftMiddleProximal, HumanoidBone::RightMiddleIntermediate, false, 0.44f },
        { HumanoidBone::LeftMiddleDistal, "LeftMiddleDistal", "Left Middle Distal", "Left Middle Distal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftMiddleIntermediate, HumanoidBone::RightMiddleDistal, false, 0.42f },

        { HumanoidBone::LeftRingProximal, "LeftRingProximal", "Left Ring Proximal", "Left Ring Proximal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftHand, HumanoidBone::RightRingProximal, false, 0.46f },
        { HumanoidBone::LeftRingIntermediate, "LeftRingIntermediate", "Left Ring Intermediate", "Left Ring Intermediate", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftRingProximal, HumanoidBone::RightRingIntermediate, false, 0.44f },
        { HumanoidBone::LeftRingDistal, "LeftRingDistal", "Left Ring Distal", "Left Ring Distal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftRingIntermediate, HumanoidBone::RightRingDistal, false, 0.42f },

        { HumanoidBone::LeftLittleProximal, "LeftLittleProximal", "Left Little Proximal", "Left Little Proximal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftHand, HumanoidBone::RightLittleProximal, false, 0.46f },
        { HumanoidBone::LeftLittleIntermediate, "LeftLittleIntermediate", "Left Little Intermediate", "Left Little Intermediate", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftLittleProximal, HumanoidBone::RightLittleIntermediate, false, 0.44f },
        { HumanoidBone::LeftLittleDistal, "LeftLittleDistal", "Left Little Distal", "Left Little Distal", BoneCategory::LeftFingers, BoneSide::Left, HumanoidBone::LeftLittleIntermediate, HumanoidBone::RightLittleDistal, false, 0.42f },

        // Right Fingers
        { HumanoidBone::RightThumbProximal, "RightThumbProximal", "Right Thumb Proximal", "Right Thumb Proximal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightHand, HumanoidBone::LeftThumbProximal, false, 0.46f },
        { HumanoidBone::RightThumbIntermediate, "RightThumbIntermediate", "Right Thumb Intermediate", "Right Thumb Intermediate", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightThumbProximal, HumanoidBone::LeftThumbIntermediate, false, 0.45f },
        { HumanoidBone::RightThumbDistal, "RightThumbDistal", "Right Thumb Distal", "Right Thumb Distal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightThumbIntermediate, HumanoidBone::LeftThumbDistal, false, 0.44f },

        { HumanoidBone::RightIndexProximal, "RightIndexProximal", "Right Index Proximal", "Right Index Proximal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightHand, HumanoidBone::LeftIndexProximal, false, 0.46f },
        { HumanoidBone::RightIndexIntermediate, "RightIndexIntermediate", "Right Index Intermediate", "Right Index Intermediate", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightIndexProximal, HumanoidBone::LeftIndexIntermediate, false, 0.44f },
        { HumanoidBone::RightIndexDistal, "RightIndexDistal", "Right Index Distal", "Right Index Distal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightIndexIntermediate, HumanoidBone::LeftIndexDistal, false, 0.42f },

        { HumanoidBone::RightMiddleProximal, "RightMiddleProximal", "Right Middle Proximal", "Right Middle Proximal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightHand, HumanoidBone::LeftMiddleProximal, false, 0.46f },
        { HumanoidBone::RightMiddleIntermediate, "RightMiddleIntermediate", "Right Middle Intermediate", "Right Middle Intermediate", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightMiddleProximal, HumanoidBone::LeftMiddleIntermediate, false, 0.44f },
        { HumanoidBone::RightMiddleDistal, "RightMiddleDistal", "Right Middle Distal", "Right Middle Distal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightMiddleIntermediate, HumanoidBone::LeftMiddleDistal, false, 0.42f },

        { HumanoidBone::RightRingProximal, "RightRingProximal", "Right Ring Proximal", "Right Ring Proximal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightHand, HumanoidBone::LeftRingProximal, false, 0.46f },
        { HumanoidBone::RightRingIntermediate, "RightRingIntermediate", "Right Ring Intermediate", "Right Ring Intermediate", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightRingProximal, HumanoidBone::LeftRingIntermediate, false, 0.44f },
        { HumanoidBone::RightRingDistal, "RightRingDistal", "Right Ring Distal", "Right Ring Distal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightRingIntermediate, HumanoidBone::LeftRingDistal, false, 0.42f },

        { HumanoidBone::RightLittleProximal, "RightLittleProximal", "Right Little Proximal", "Right Little Proximal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightHand, HumanoidBone::LeftLittleProximal, false, 0.46f },
        { HumanoidBone::RightLittleIntermediate, "RightLittleIntermediate", "Right Little Intermediate", "Right Little Intermediate", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightLittleProximal, HumanoidBone::LeftLittleIntermediate, false, 0.44f },
        { HumanoidBone::RightLittleDistal, "RightLittleDistal", "Right Little Distal", "Right Little Distal", BoneCategory::RightFingers, BoneSide::Right, HumanoidBone::RightLittleIntermediate, HumanoidBone::LeftLittleDistal, false, 0.42f },

        // Auxiliary / Twist
        { HumanoidBone::LeftUpperArmTwist, "LeftUpperArmTwist", "Left Upper Arm Twist", "Left Upper Arm Twist", BoneCategory::AuxiliaryTwist, BoneSide::Left, HumanoidBone::LeftUpperArm, HumanoidBone::RightUpperArmTwist, false, 0.72f },
        { HumanoidBone::LeftLowerArmTwist, "LeftLowerArmTwist", "Left Lower Arm Twist", "Left Lower Arm Twist", BoneCategory::AuxiliaryTwist, BoneSide::Left, HumanoidBone::LeftLowerArm, HumanoidBone::RightLowerArmTwist, false, 0.54f },
        { HumanoidBone::RightUpperArmTwist, "RightUpperArmTwist", "Right Upper Arm Twist", "Right Upper Arm Twist", BoneCategory::AuxiliaryTwist, BoneSide::Right, HumanoidBone::RightUpperArm, HumanoidBone::LeftUpperArmTwist, false, 0.72f },
        { HumanoidBone::RightLowerArmTwist, "RightLowerArmTwist", "Right Lower Arm Twist", "Right Lower Arm Twist", BoneCategory::AuxiliaryTwist, BoneSide::Right, HumanoidBone::RightLowerArm, HumanoidBone::LeftLowerArmTwist, false, 0.54f },
        { HumanoidBone::LeftUpperLegTwist, "LeftUpperLegTwist", "Left Upper Leg Twist", "Left Upper Leg Twist", BoneCategory::AuxiliaryTwist, BoneSide::Left, HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLegTwist, false, 0.40f },
        { HumanoidBone::LeftLowerLegTwist, "LeftLowerLegTwist", "Left Lower Leg Twist", "Left Lower Leg Twist", BoneCategory::AuxiliaryTwist, BoneSide::Left, HumanoidBone::LeftLowerLeg, HumanoidBone::RightLowerLegTwist, false, 0.18f },
        { HumanoidBone::RightUpperLegTwist, "RightUpperLegTwist", "Right Upper Leg Twist", "Right Upper Leg Twist", BoneCategory::AuxiliaryTwist, BoneSide::Right, HumanoidBone::RightUpperLeg, HumanoidBone::LeftUpperLegTwist, false, 0.40f },
        { HumanoidBone::RightLowerLegTwist, "RightLowerLegTwist", "Right Lower Leg Twist", "Right Lower Leg Twist", BoneCategory::AuxiliaryTwist, BoneSide::Right, HumanoidBone::RightLowerLeg, HumanoidBone::LeftLowerLegTwist, false, 0.18f },
    };

    int idx = static_cast<int>(bone);
    if (idx >= 0 && idx < static_cast<int>(HumanoidBone::Count)) {
        return s_boneInfoTable[idx];
    }
    static const HumanoidBoneInfo s_invalid{ HumanoidBone::None, "None", "None", "None", BoneCategory::RootPelvis, BoneSide::Center, HumanoidBone::None, HumanoidBone::None, false, 0.0f };
    return s_invalid;
}

inline const char* GetCategoryName(BoneCategory cat, bool russian = false) {
    switch (cat) {
        case BoneCategory::RootPelvis: return "Root & Pelvis";
        case BoneCategory::SpineTorso: return "Spine & Torso";
        case BoneCategory::HeadFace:   return "Head & Face";
        case BoneCategory::LeftArm:    return "Left Arm";
        case BoneCategory::RightArm:   return "Right Arm";
        case BoneCategory::LeftLeg:    return "Left Leg";
        case BoneCategory::RightLeg:   return "Right Leg";
        case BoneCategory::LeftFingers: return "Left Fingers";
        case BoneCategory::RightFingers: return "Right Fingers";
        case BoneCategory::AuxiliaryTwist: return "Twist & Auxiliary";
        default: return "Unknown";
    }
}

inline std::vector<HumanoidBone> GetBonesInCategory(BoneCategory cat) {
    std::vector<HumanoidBone> result;
    for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
        HumanoidBone b = static_cast<HumanoidBone>(i);
        if (GetHumanoidBoneInfo(b).category == cat) {
            result.push_back(b);
        }
    }
    return result;
}

} // namespace EngineAnim
