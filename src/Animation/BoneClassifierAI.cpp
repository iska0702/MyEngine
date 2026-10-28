#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include "BoneClassifierAI.h"
#include "Renderer/Renderer.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace EngineAnim {

BoneClassifierAI& BoneClassifierAI::Get() {
    static BoneClassifierAI s_instance;
    return s_instance;
}

BoneClassifierAI::BoneClassifierAI() {
    Initialize();
}

void BoneClassifierAI::Initialize() {
    if (m_initialized) return;

    SeedPretrainedWeights();
    SeedPretrainedDataset();

    // 1. Load model directly from encrypted PAK archives
    auto pakData = Renderer::LoadFromPak(L"Content/Config/bone_ai_model.json");
    if (pakData.empty()) pakData = Renderer::LoadFromPak(L"Config/bone_ai_model.json");
    if (pakData.empty()) pakData = Renderer::LoadFromPak(L"Content/bone_ai_model.json");
    if (pakData.empty()) pakData = Renderer::LoadFromPak(L"bone_ai_model.json");

    if (!pakData.empty()) {
        std::string jsonStr(pakData.begin(), pakData.end());
        if (LoadModelFromString(jsonStr)) {
            LOG("BoneClassifierAI: Neural model loaded successfully from encrypted .PAK!");
            m_initialized = true;
            return;
        }
    }

    // 2. Fallback to local files if present
    std::vector<std::string> searchPaths = {
        "Config/bone_ai_model.json",
        "../Config/bone_ai_model.json",
        "src/Content/Config/bone_ai_model.json",
        "../src/Content/Config/bone_ai_model.json"
    };

    for (const auto& sp : searchPaths) {
        if (std::filesystem::exists(sp)) {
            if (LoadModel(sp)) {
                break;
            }
        }
    }

    m_initialized = true;
}

std::vector<std::string> BoneClassifierAI::TokenizeBoneName(const std::string& name) {
    std::vector<std::string> tokens;
    if (name.empty()) return tokens;

    std::string current;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];

        // Delimiter check
        if (c == '_' || c == '.' || c == ':' || c == '-' || c == '|' || c == '/' || c == '\\' || c == ' ' || c == '\t') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        // CamelCase transition check (e.g. "LeftArm" -> "Left", "Arm")
        if (std::isupper(static_cast<unsigned char>(c)) && !current.empty()) {
            bool prevLower = std::islower(static_cast<unsigned char>(name[i - 1]));
            bool nextLower = (i + 1 < name.size()) && std::islower(static_cast<unsigned char>(name[i + 1]));
            if (prevLower || nextLower) {
                tokens.push_back(current);
                current.clear();
            }
        }

        // Digit transition check
        if (std::isdigit(static_cast<unsigned char>(c)) && !current.empty() && !std::isdigit(static_cast<unsigned char>(name[i - 1]))) {
            tokens.push_back(current);
            current.clear();
        } else if (!std::isdigit(static_cast<unsigned char>(c)) && !current.empty() && std::isdigit(static_cast<unsigned char>(name[i - 1]))) {
            tokens.push_back(current);
            current.clear();
        }

        current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

void BoneClassifierAI::SeedPretrainedWeights() {
    m_tokenWeights.clear();
    m_boneBiases.clear();

    auto AddRule = [this](const std::string& token, HumanoidBone bone, float weight) {
        std::string t = token;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        m_tokenWeights[t][static_cast<int16_t>(bone)] += weight;
    };

    // --- Root & Pelvis ---
    AddRule("root", HumanoidBone::Root, 10.0f);
    AddRule("origin", HumanoidBone::Root, 8.0f);
    AddRule("hips", HumanoidBone::Hips, 12.0f);
    AddRule("hip", HumanoidBone::Hips, 10.0f);
    AddRule("pelvis", HumanoidBone::Hips, 12.0f);
    AddRule("waist", HumanoidBone::Hips, 7.0f);

    // --- Spine & Torso ---
    AddRule("spine", HumanoidBone::Spine, 8.0f);
    AddRule("spine01", HumanoidBone::Spine, 12.0f);
    AddRule("spine1", HumanoidBone::Spine, 10.0f);
    AddRule("spine02", HumanoidBone::Spine1, 12.0f);
    AddRule("spine2", HumanoidBone::Spine1, 10.0f);
    AddRule("spine03", HumanoidBone::Spine2, 12.0f);
    AddRule("spine3", HumanoidBone::Spine2, 10.0f);
    AddRule("chest", HumanoidBone::Chest, 12.0f);
    AddRule("upperchest", HumanoidBone::UpperChest, 12.0f);
    AddRule("torso", HumanoidBone::Spine, 6.0f);
    AddRule("abdomen", HumanoidBone::Spine, 8.0f);
    AddRule("ribcage", HumanoidBone::Chest, 9.0f);

    // --- Head & Face ---
    AddRule("neck", HumanoidBone::Neck, 12.0f);
    AddRule("neck01", HumanoidBone::Neck, 12.0f);
    AddRule("head", HumanoidBone::Head, 14.0f);
    AddRule("jaw", HumanoidBone::Jaw, 12.0f);
    AddRule("lefteye", HumanoidBone::LeftEye, 12.0f);
    AddRule("righteye", HumanoidBone::RightEye, 12.0f);
    AddRule("eye_l", HumanoidBone::LeftEye, 12.0f);
    AddRule("eye_r", HumanoidBone::RightEye, 12.0f);

    // --- Left Arm ---
    AddRule("leftshoulder", HumanoidBone::LeftShoulder, 12.0f);
    AddRule("leftclavicle", HumanoidBone::LeftShoulder, 12.0f);
    AddRule("clavicle_l", HumanoidBone::LeftShoulder, 12.0f);
    AddRule("shoulder_l", HumanoidBone::LeftShoulder, 12.0f);
    AddRule("lshoulder", HumanoidBone::LeftShoulder, 12.0f);
    AddRule("lcollar", HumanoidBone::LeftShoulder, 12.0f);

    AddRule("leftupperarm", HumanoidBone::LeftUpperArm, 14.0f);
    AddRule("leftarm", HumanoidBone::LeftUpperArm, 10.0f);
    AddRule("upperarm_l", HumanoidBone::LeftUpperArm, 14.0f);
    AddRule("arm_l", HumanoidBone::LeftUpperArm, 10.0f);
    AddRule("lshldr", HumanoidBone::LeftUpperArm, 12.0f);
    AddRule("l_arm", HumanoidBone::LeftUpperArm, 10.0f);

    AddRule("leftforearm", HumanoidBone::LeftLowerArm, 14.0f);
    AddRule("leftlowerarm", HumanoidBone::LeftLowerArm, 14.0f);
    AddRule("forearm_l", HumanoidBone::LeftLowerArm, 14.0f);
    AddRule("lowerarm_l", HumanoidBone::LeftLowerArm, 14.0f);
    AddRule("lforearm", HumanoidBone::LeftLowerArm, 12.0f);
    AddRule("lelbow", HumanoidBone::LeftLowerArm, 9.0f);

    AddRule("lefthand", HumanoidBone::LeftHand, 14.0f);
    AddRule("hand_l", HumanoidBone::LeftHand, 14.0f);
    AddRule("lhand", HumanoidBone::LeftHand, 12.0f);
    AddRule("lwrist", HumanoidBone::LeftHand, 10.0f);

    // --- Right Arm ---
    AddRule("rightshoulder", HumanoidBone::RightShoulder, 12.0f);
    AddRule("rightclavicle", HumanoidBone::RightShoulder, 12.0f);
    AddRule("clavicle_r", HumanoidBone::RightShoulder, 12.0f);
    AddRule("shoulder_r", HumanoidBone::RightShoulder, 12.0f);
    AddRule("rshoulder", HumanoidBone::RightShoulder, 12.0f);
    AddRule("rcollar", HumanoidBone::RightShoulder, 12.0f);

    AddRule("rightupperarm", HumanoidBone::RightUpperArm, 14.0f);
    AddRule("rightarm", HumanoidBone::RightUpperArm, 10.0f);
    AddRule("upperarm_r", HumanoidBone::RightUpperArm, 14.0f);
    AddRule("arm_r", HumanoidBone::RightUpperArm, 10.0f);
    AddRule("rshldr", HumanoidBone::RightUpperArm, 12.0f);
    AddRule("r_arm", HumanoidBone::RightUpperArm, 10.0f);

    AddRule("rightforearm", HumanoidBone::RightLowerArm, 14.0f);
    AddRule("rightlowerarm", HumanoidBone::RightLowerArm, 14.0f);
    AddRule("forearm_r", HumanoidBone::RightLowerArm, 14.0f);
    AddRule("lowerarm_r", HumanoidBone::RightLowerArm, 14.0f);
    AddRule("rforearm", HumanoidBone::RightLowerArm, 12.0f);
    AddRule("relbow", HumanoidBone::RightLowerArm, 9.0f);

    AddRule("righthand", HumanoidBone::RightHand, 14.0f);
    AddRule("hand_r", HumanoidBone::RightHand, 14.0f);
    AddRule("rhand", HumanoidBone::RightHand, 12.0f);
    AddRule("rwrist", HumanoidBone::RightHand, 10.0f);

    // --- Left Leg ---
    AddRule("leftupleg", HumanoidBone::LeftUpperLeg, 14.0f);
    AddRule("leftupperleg", HumanoidBone::LeftUpperLeg, 14.0f);
    AddRule("leftthigh", HumanoidBone::LeftUpperLeg, 14.0f);
    AddRule("thigh_l", HumanoidBone::LeftUpperLeg, 14.0f);
    AddRule("lthigh", HumanoidBone::LeftUpperLeg, 12.0f);
    AddRule("leg_l", HumanoidBone::LeftUpperLeg, 8.0f);

    AddRule("leftleg", HumanoidBone::LeftLowerLeg, 10.0f);
    AddRule("leftlowerleg", HumanoidBone::LeftLowerLeg, 14.0f);
    AddRule("leftcalf", HumanoidBone::LeftLowerLeg, 14.0f);
    AddRule("leftshin", HumanoidBone::LeftLowerLeg, 14.0f);
    AddRule("calf_l", HumanoidBone::LeftLowerLeg, 14.0f);
    AddRule("shin_l", HumanoidBone::LeftLowerLeg, 14.0f);
    AddRule("lshin", HumanoidBone::LeftLowerLeg, 12.0f);
    AddRule("lknee", HumanoidBone::LeftLowerLeg, 8.0f);

    AddRule("leftfoot", HumanoidBone::LeftFoot, 14.0f);
    AddRule("foot_l", HumanoidBone::LeftFoot, 14.0f);
    AddRule("lfoot", HumanoidBone::LeftFoot, 12.0f);
    AddRule("leftankle", HumanoidBone::LeftFoot, 12.0f);

    AddRule("lefttoebase", HumanoidBone::LeftToes, 12.0f);
    AddRule("lefttoe", HumanoidBone::LeftToes, 12.0f);
    AddRule("toe_l", HumanoidBone::LeftToes, 12.0f);
    AddRule("ball_l", HumanoidBone::LeftToes, 12.0f);
    AddRule("ltoe", HumanoidBone::LeftToes, 12.0f);

    // --- Right Leg ---
    AddRule("rightupleg", HumanoidBone::RightUpperLeg, 14.0f);
    AddRule("rightupperleg", HumanoidBone::RightUpperLeg, 14.0f);
    AddRule("rightthigh", HumanoidBone::RightUpperLeg, 14.0f);
    AddRule("thigh_r", HumanoidBone::RightUpperLeg, 14.0f);
    AddRule("rthigh", HumanoidBone::RightUpperLeg, 12.0f);
    AddRule("leg_r", HumanoidBone::RightUpperLeg, 8.0f);

    AddRule("rightleg", HumanoidBone::RightLowerLeg, 10.0f);
    AddRule("rightlowerleg", HumanoidBone::RightLowerLeg, 14.0f);
    AddRule("rightcalf", HumanoidBone::RightLowerLeg, 14.0f);
    AddRule("rightshin", HumanoidBone::RightLowerLeg, 14.0f);
    AddRule("calf_r", HumanoidBone::RightLowerLeg, 14.0f);
    AddRule("shin_r", HumanoidBone::RightLowerLeg, 14.0f);
    AddRule("rshin", HumanoidBone::RightLowerLeg, 12.0f);
    AddRule("rknee", HumanoidBone::RightLowerLeg, 8.0f);

    AddRule("rightfoot", HumanoidBone::RightFoot, 14.0f);
    AddRule("foot_r", HumanoidBone::RightFoot, 14.0f);
    AddRule("rfoot", HumanoidBone::RightFoot, 12.0f);
    AddRule("rightankle", HumanoidBone::RightFoot, 12.0f);

    AddRule("righttoebase", HumanoidBone::RightToes, 12.0f);
    AddRule("righttoe", HumanoidBone::RightToes, 12.0f);
    AddRule("toe_r", HumanoidBone::RightToes, 12.0f);
    AddRule("ball_r", HumanoidBone::RightToes, 12.0f);
    AddRule("rtoe", HumanoidBone::RightToes, 12.0f);

    // --- Fingers (Left) ---
    AddRule("thumb_01_l", HumanoidBone::LeftThumbProximal, 14.0f);
    AddRule("thumb_02_l", HumanoidBone::LeftThumbIntermediate, 14.0f);
    AddRule("thumb_03_l", HumanoidBone::LeftThumbDistal, 14.0f);
    AddRule("index_01_l", HumanoidBone::LeftIndexProximal, 14.0f);
    AddRule("index_02_l", HumanoidBone::LeftIndexIntermediate, 14.0f);
    AddRule("index_03_l", HumanoidBone::LeftIndexDistal, 14.0f);
    AddRule("middle_01_l", HumanoidBone::LeftMiddleProximal, 14.0f);
    AddRule("middle_02_l", HumanoidBone::LeftMiddleIntermediate, 14.0f);
    AddRule("middle_03_l", HumanoidBone::LeftMiddleDistal, 14.0f);
    AddRule("ring_01_l", HumanoidBone::LeftRingProximal, 14.0f);
    AddRule("ring_02_l", HumanoidBone::LeftRingIntermediate, 14.0f);
    AddRule("ring_03_l", HumanoidBone::LeftRingDistal, 14.0f);
    AddRule("pinky_01_l", HumanoidBone::LeftLittleProximal, 14.0f);
    AddRule("pinky_02_l", HumanoidBone::LeftLittleIntermediate, 14.0f);
    AddRule("pinky_03_l", HumanoidBone::LeftLittleDistal, 14.0f);

    // --- Fingers (Right) ---
    AddRule("thumb_01_r", HumanoidBone::RightThumbProximal, 14.0f);
    AddRule("thumb_02_r", HumanoidBone::RightThumbIntermediate, 14.0f);
    AddRule("thumb_03_r", HumanoidBone::RightThumbDistal, 14.0f);
    AddRule("index_01_r", HumanoidBone::RightIndexProximal, 14.0f);
    AddRule("index_02_r", HumanoidBone::RightIndexIntermediate, 14.0f);
    AddRule("index_03_r", HumanoidBone::RightIndexDistal, 14.0f);
    AddRule("middle_01_r", HumanoidBone::RightMiddleProximal, 14.0f);
    AddRule("middle_02_r", HumanoidBone::RightMiddleIntermediate, 14.0f);
    AddRule("middle_03_r", HumanoidBone::RightMiddleDistal, 14.0f);
    AddRule("ring_01_r", HumanoidBone::RightRingProximal, 14.0f);
    AddRule("ring_02_r", HumanoidBone::RightRingIntermediate, 14.0f);
    AddRule("ring_03_r", HumanoidBone::RightRingDistal, 14.0f);
    AddRule("pinky_01_r", HumanoidBone::RightLittleProximal, 14.0f);
    AddRule("pinky_02_r", HumanoidBone::RightLittleIntermediate, 14.0f);
    AddRule("pinky_03_r", HumanoidBone::RightLittleDistal, 14.0f);
}

void BoneClassifierAI::SeedPretrainedDataset() {
    m_dataset.clear();

    auto AddSample = [this](const std::string& skel, const std::string& name, HumanoidBone target, int16_t parent, float y, float x, float z, int depth) {
        BoneTrainingSample s;
        s.skeletonName = skel;
        s.boneName = name;
        s.targetBone = target;
        s.parentIndex = parent;
        s.relativeY = y;
        s.relativeX = x;
        s.relativeZ = z;
        s.depth = depth;
        m_dataset.push_back(s);
    };

    // Mixamo Skeleton Preset
    AddSample("Mixamo", "mixamorig:Hips", HumanoidBone::Hips, -1, 0.55f, 0.0f, 0.0f, 0);
    AddSample("Mixamo", "mixamorig:Spine", HumanoidBone::Spine, 0, 0.60f, 0.0f, 0.0f, 1);
    AddSample("Mixamo", "mixamorig:Spine1", HumanoidBone::Spine1, 1, 0.66f, 0.0f, 0.0f, 2);
    AddSample("Mixamo", "mixamorig:Spine2", HumanoidBone::Chest, 2, 0.72f, 0.0f, 0.0f, 3);
    AddSample("Mixamo", "mixamorig:Neck", HumanoidBone::Neck, 3, 0.84f, 0.0f, 0.0f, 4);
    AddSample("Mixamo", "mixamorig:Head", HumanoidBone::Head, 4, 0.92f, 0.0f, 0.0f, 5);
    AddSample("Mixamo", "mixamorig:LeftShoulder", HumanoidBone::LeftShoulder, 3, 0.80f, -0.15f, 0.0f, 4);
    AddSample("Mixamo", "mixamorig:LeftArm", HumanoidBone::LeftUpperArm, 6, 0.78f, -0.25f, 0.0f, 5);
    AddSample("Mixamo", "mixamorig:LeftForeArm", HumanoidBone::LeftLowerArm, 7, 0.60f, -0.40f, 0.0f, 6);
    AddSample("Mixamo", "mixamorig:LeftHand", HumanoidBone::LeftHand, 8, 0.48f, -0.55f, 0.0f, 7);
    AddSample("Mixamo", "mixamorig:RightShoulder", HumanoidBone::RightShoulder, 3, 0.80f, 0.15f, 0.0f, 4);
    AddSample("Mixamo", "mixamorig:RightArm", HumanoidBone::RightUpperArm, 10, 0.78f, 0.25f, 0.0f, 5);
    AddSample("Mixamo", "mixamorig:RightForeArm", HumanoidBone::RightLowerArm, 11, 0.60f, 0.40f, 0.0f, 6);
    AddSample("Mixamo", "mixamorig:RightHand", HumanoidBone::RightHand, 12, 0.48f, 0.55f, 0.0f, 7);
    AddSample("Mixamo", "mixamorig:LeftUpLeg", HumanoidBone::LeftUpperLeg, 0, 0.50f, -0.12f, 0.0f, 1);
    AddSample("Mixamo", "mixamorig:LeftLeg", HumanoidBone::LeftLowerLeg, 14, 0.28f, -0.12f, 0.0f, 2);
    AddSample("Mixamo", "mixamorig:LeftFoot", HumanoidBone::LeftFoot, 15, 0.08f, -0.12f, 0.0f, 3);
    AddSample("Mixamo", "mixamorig:LeftToeBase", HumanoidBone::LeftToes, 16, 0.02f, -0.12f, 0.1f, 4);
    AddSample("Mixamo", "mixamorig:RightUpLeg", HumanoidBone::RightUpperLeg, 0, 0.50f, 0.12f, 0.0f, 1);
    AddSample("Mixamo", "mixamorig:RightLeg", HumanoidBone::RightLowerLeg, 18, 0.28f, 0.12f, 0.0f, 2);
    AddSample("Mixamo", "mixamorig:RightFoot", HumanoidBone::RightFoot, 19, 0.08f, 0.12f, 0.0f, 3);
    AddSample("Mixamo", "mixamorig:RightToeBase", HumanoidBone::RightToes, 20, 0.02f, 0.12f, 0.1f, 4);

    // Unreal Mannequin Preset
    AddSample("UnrealMannequin", "pelvis", HumanoidBone::Hips, -1, 0.55f, 0.0f, 0.0f, 0);
    AddSample("UnrealMannequin", "spine_01", HumanoidBone::Spine, 0, 0.60f, 0.0f, 0.0f, 1);
    AddSample("UnrealMannequin", "spine_02", HumanoidBone::Spine1, 1, 0.66f, 0.0f, 0.0f, 2);
    AddSample("UnrealMannequin", "spine_03", HumanoidBone::Chest, 2, 0.72f, 0.0f, 0.0f, 3);
    AddSample("UnrealMannequin", "neck_01", HumanoidBone::Neck, 3, 0.84f, 0.0f, 0.0f, 4);
    AddSample("UnrealMannequin", "head", HumanoidBone::Head, 4, 0.92f, 0.0f, 0.0f, 5);
    AddSample("UnrealMannequin", "clavicle_l", HumanoidBone::LeftShoulder, 3, 0.80f, -0.15f, 0.0f, 4);
    AddSample("UnrealMannequin", "upperarm_l", HumanoidBone::LeftUpperArm, 6, 0.78f, -0.25f, 0.0f, 5);
    AddSample("UnrealMannequin", "lowerarm_l", HumanoidBone::LeftLowerArm, 7, 0.60f, -0.40f, 0.0f, 6);
    AddSample("UnrealMannequin", "hand_l", HumanoidBone::LeftHand, 8, 0.48f, -0.55f, 0.0f, 7);
    AddSample("UnrealMannequin", "clavicle_r", HumanoidBone::RightShoulder, 3, 0.80f, 0.15f, 0.0f, 4);
    AddSample("UnrealMannequin", "upperarm_r", HumanoidBone::RightUpperArm, 10, 0.78f, 0.25f, 0.0f, 5);
    AddSample("UnrealMannequin", "lowerarm_r", HumanoidBone::RightLowerArm, 11, 0.60f, 0.40f, 0.0f, 6);
    AddSample("UnrealMannequin", "hand_r", HumanoidBone::RightHand, 12, 0.48f, 0.55f, 0.0f, 7);
    AddSample("UnrealMannequin", "thigh_l", HumanoidBone::LeftUpperLeg, 0, 0.50f, -0.12f, 0.0f, 1);
    AddSample("UnrealMannequin", "calf_l", HumanoidBone::LeftLowerLeg, 14, 0.28f, -0.12f, 0.0f, 2);
    AddSample("UnrealMannequin", "foot_l", HumanoidBone::LeftFoot, 15, 0.08f, -0.12f, 0.0f, 3);
    AddSample("UnrealMannequin", "ball_l", HumanoidBone::LeftToes, 16, 0.02f, -0.12f, 0.1f, 4);
    AddSample("UnrealMannequin", "thigh_r", HumanoidBone::RightUpperLeg, 0, 0.50f, 0.12f, 0.0f, 1);
    AddSample("UnrealMannequin", "calf_r", HumanoidBone::RightLowerLeg, 18, 0.28f, 0.12f, 0.0f, 2);
    AddSample("UnrealMannequin", "foot_r", HumanoidBone::RightFoot, 19, 0.08f, 0.12f, 0.0f, 3);
    AddSample("UnrealMannequin", "ball_r", HumanoidBone::RightToes, 20, 0.02f, 0.12f, 0.1f, 4);
}

float BoneClassifierAI::ComputeScore(const std::vector<std::string>& tokens, HumanoidBone bone, float relY, float relX, int depth, int16_t parentBoneId) const {
    const auto& info = GetHumanoidBoneInfo(bone);
    int16_t bId = static_cast<int16_t>(bone);

    float score = 0.0f;

    // 1. Bias
    auto itBias = m_boneBiases.find(bId);
    if (itBias != m_boneBiases.end()) {
        score += itBias->second;
    }

    // 2. Token Matching
    for (const auto& token : tokens) {
        auto itToken = m_tokenWeights.find(token);
        if (itToken != m_tokenWeights.end()) {
            auto itBoneWeight = itToken->second.find(bId);
            if (itBoneWeight != itToken->second.end()) {
                score += itBoneWeight->second;
            }
        }

        // Substring / partial matching
        std::string boneIdLower = info.idName;
        std::transform(boneIdLower.begin(), boneIdLower.end(), boneIdLower.begin(), [](unsigned char c) { return std::tolower(c); });

        if (token == boneIdLower) {
            score += 15.0f;
        } else if (boneIdLower.find(token) != std::string::npos && token.size() >= 3) {
            score += 4.0f;
        }
    }

    // 3. Side Compatibility (Left / Right / Center)
    bool hasLeftToken = false;
    bool hasRightToken = false;
    for (const auto& t : tokens) {
        if (t == "l" || t == "left" || t == "lft" || t.find("_l") != std::string::npos || t.find(".l") != std::string::npos) hasLeftToken = true;
        if (t == "r" || t == "right" || t == "rgt" || t.find("_r") != std::string::npos || t.find(".r") != std::string::npos) hasRightToken = true;
    }

    if (info.side == BoneSide::Left) {
        if (hasLeftToken) score += 6.0f;
        if (hasRightToken) score -= 20.0f;
        if (relX < -0.01f) score += 2.0f;
    } else if (info.side == BoneSide::Right) {
        if (hasRightToken) score += 6.0f;
        if (hasLeftToken) score -= 20.0f;
        if (relX > 0.01f) score += 2.0f;
    } else { // Center
        if (hasLeftToken || hasRightToken) score -= 10.0f;
        if (std::abs(relX) < 0.1f) score += 1.5f;
    }

    // 4. Height / Vertical Spatial Prior
    float expY = info.defaultRelativeY;
    float yDiff = std::abs(relY - expY);
    score += (1.0f - (std::min)(yDiff, 1.0f)) * 3.0f;

    // 5. Parent Relation Prior
    if (parentBoneId >= 0 && info.defaultParent != HumanoidBone::None) {
        if (parentBoneId == static_cast<int16_t>(info.defaultParent)) {
            score += 4.0f;
        }
    }

    return score;
}

void BoneClassifierAI::NormalizeScores(std::vector<std::pair<HumanoidBone, float>>& scores) const {
    if (scores.empty()) return;

    // Compute max for numerical stability
    float maxScore = -1e9f;
    for (const auto& p : scores) {
        if (p.second > maxScore) maxScore = p.second;
    }

    float sumExp = 0.0f;
    for (auto& p : scores) {
        p.second = std::exp(p.second - maxScore);
        sumExp += p.second;
    }

    if (sumExp > 1e-6f) {
        for (auto& p : scores) {
            p.second = (p.second / sumExp) * 100.0f;
        }
    }

    std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
}

std::vector<std::pair<HumanoidBone, float>> BoneClassifierAI::PredictBoneCandidates(
    const std::string& boneName,
    int16_t parentIndex,
    float relativeY,
    float relativeX,
    float relativeZ,
    int depth,
    const OzzSkeleton* skeleton
) {
    std::vector<std::string> tokens = TokenizeBoneName(boneName);
    std::vector<std::pair<HumanoidBone, float>> candidates;

    int16_t parentBoneId = -1;

    for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
        HumanoidBone bone = static_cast<HumanoidBone>(i);
        float rawScore = ComputeScore(tokens, bone, relativeY, relativeX, depth, parentBoneId);
        candidates.push_back({ bone, rawScore });
    }

    NormalizeScores(candidates);
    return candidates;
}

HumanoidBoneMapping BoneClassifierAI::ClassifySkeleton(const OzzSkeleton& skeleton) {
    HumanoidBoneMapping mapping;
    int numJoints = skeleton.GetNumJoints();
    if (numJoints == 0) return mapping;

    // 1. Calculate joint positions & bounding box to normalize coordinates
    const auto& modelMats = skeleton.GetBindPoseModelMatrices();
    float minY = 1e9f, maxY = -1e9f;
    float minX = 1e9f, maxX = -1e9f;

    std::vector<Vec3> jointPositions(numJoints);
    for (int i = 0; i < numJoints; ++i) {
        Vec3 pos(0, 0, 0);
        if (i < static_cast<int>(modelMats.size())) {
            pos = Vec3(modelMats[i].m[3][0], modelMats[i].m[3][1], modelMats[i].m[3][2]);
        } else {
            pos = skeleton.GetJoint(i).restPose.translation;
        }
        jointPositions[i] = pos;

        if (pos.y < minY) minY = pos.y;
        if (pos.y > maxY) maxY = pos.y;
        if (pos.x < minX) minX = pos.x;
        if (pos.x > maxX) maxX = pos.x;
    }

    float height = (maxY - minY > 1e-3f) ? (maxY - minY) : 1.0f;
    float width  = (maxX - minX > 1e-3f) ? (maxX - minX) : 1.0f;

    // Calculate depths
    std::vector<int> depths(numJoints, 0);
    for (int i = 0; i < numJoints; ++i) {
        int d = 0;
        int p = skeleton.GetParentIndex(i);
        while (p >= 0 && d < 100) {
            d++;
            p = skeleton.GetParentIndex(p);
        }
        depths[i] = d;
    }

    // 2. Predict candidates for each joint
    struct JointCandidateSet {
        int jointIdx;
        std::vector<std::pair<HumanoidBone, float>> predictions;
    };
    std::vector<JointCandidateSet> allJointCandidates(numJoints);

    for (int i = 0; i < numJoints; ++i) {
        const auto& joint = skeleton.GetJoint(i);
        float relY = (jointPositions[i].y - minY) / height;
        float relX = (jointPositions[i].x) / (width * 0.5f);
        float relZ = jointPositions[i].z;

        allJointCandidates[i].jointIdx = i;
        allJointCandidates[i].predictions = PredictBoneCandidates(
            joint.name,
            skeleton.GetParentIndex(i),
            relY, relX, relZ,
            depths[i],
            &skeleton
        );
    }

    // 3. Global Assignment with Conflict Resolution
    // Iterate over candidate pairs sorted by confidence
    struct AssignmentCandidate {
        int jointIdx;
        HumanoidBone bone;
        float confidence;
    };
    std::vector<AssignmentCandidate> sortedCandidates;

    for (int i = 0; i < numJoints; ++i) {
        for (const auto& pred : allJointCandidates[i].predictions) {
            if (pred.second > 1.0f) { // Only consider plausible predictions
                sortedCandidates.push_back({ i, pred.first, pred.second });
            }
        }
    }

    std::sort(sortedCandidates.begin(), sortedCandidates.end(), [](const auto& a, const auto& b) {
        return a.confidence > b.confidence;
    });

    std::unordered_map<HumanoidBone, bool> assignedBones;
    std::unordered_map<int, bool> assignedJoints;

    for (const auto& cand : sortedCandidates) {
        if (assignedBones[cand.bone] || assignedJoints[cand.jointIdx]) {
            continue;
        }

        // Only assign if confidence is above minimal threshold
        if (cand.confidence >= 5.0f) {
            mapping.SetMapping(cand.bone, cand.jointIdx, cand.confidence / 100.0f);
            assignedBones[cand.bone] = true;
            assignedJoints[cand.jointIdx] = true;
        }
    }

    return mapping;
}

float BoneClassifierAI::CalculateOverallConfidence(const HumanoidBoneMapping& mapping) const {
    if (mapping.jointToBone.empty()) return 0.0f;

    float total = 0.0f;
    int count = 0;

    // Required bones weight more
    for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
        HumanoidBone b = static_cast<HumanoidBone>(i);
        const auto& info = GetHumanoidBoneInfo(b);
        if (!info.isRequired) continue;

        int jIdx = mapping.GetJoint(b);
        if (jIdx >= 0) {
            total += mapping.GetConfidence(jIdx);
        }
        count++;
    }

    return (count > 0) ? (total / static_cast<float>(count)) * 100.0f : 0.0f;
}

void BoneClassifierAI::AddTrainingSample(const BoneTrainingSample& sample) {
    m_dataset.push_back(sample);
}

void BoneClassifierAI::AddSkeletonToDataset(const std::string& skeletonName, const OzzSkeleton& skeleton, const HumanoidBoneMapping& mapping) {
    int numJoints = skeleton.GetNumJoints();
    if (numJoints == 0) return;

    for (int i = 0; i < numJoints; ++i) {
        HumanoidBone bone = mapping.GetHumanoidBone(i);
        if (bone != HumanoidBone::None) {
            BoneTrainingSample s;
            s.skeletonName = skeletonName;
            s.boneName = skeleton.GetJoint(i).name;
            s.targetBone = bone;
            s.parentIndex = skeleton.GetParentIndex(i);
            m_dataset.push_back(s);
        }
    }

    // Auto train if enabled
    if (m_autoLearn) {
        TrainModel(20, 0.1f);
        SaveModel();
    }
}

void BoneClassifierAI::LearnRule(const std::string& tokenOrSubstring, HumanoidBone targetBone, float weightBoost) {
    std::string t = tokenOrSubstring;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    m_tokenWeights[t][static_cast<int16_t>(targetBone)] += weightBoost;
    SaveModel();
}

void BoneClassifierAI::LearnCorrection(const std::string& boneName, HumanoidBone correctBone, const OzzSkeleton* skeleton, int jointIdx) {
    if (boneName.empty() || correctBone == HumanoidBone::None) return;

    std::vector<std::string> tokens = TokenizeBoneName(boneName);
    for (const auto& token : tokens) {
        m_tokenWeights[token][static_cast<int16_t>(correctBone)] += 8.0f;
    }

    // Add to dataset
    BoneTrainingSample s;
    s.skeletonName = "UserCorrection";
    s.boneName = boneName;
    s.targetBone = correctBone;
    if (skeleton && jointIdx >= 0 && jointIdx < skeleton->GetNumJoints()) {
        s.parentIndex = skeleton->GetParentIndex(jointIdx);
    }
    m_dataset.push_back(s);

    if (m_autoLearn) {
        TrainModel(15, 0.08f);
        SaveModel();
    }
}

TrainMetrics BoneClassifierAI::TrainModel(int epochs, float learningRate) {
    TrainMetrics metrics;
    metrics.epochsRun = epochs;
    metrics.sampleCount = static_cast<int>(m_dataset.size());
    metrics.vocabularySize = static_cast<int>(m_tokenWeights.size());

    if (m_dataset.empty()) {
        m_lastMetrics = metrics;
        return metrics;
    }

    // Training loop
    float initialLoss = 0.0f;
    float finalLoss = 0.0f;
    int correctCount = 0;

    for (int epoch = 0; epoch < epochs; ++epoch) {
        float epochLoss = 0.0f;
        correctCount = 0;

        for (const auto& sample : m_dataset) {
            std::vector<std::string> tokens = TokenizeBoneName(sample.boneName);
            int16_t targetId = static_cast<int16_t>(sample.targetBone);

            // Compute scores
            std::vector<float> scores(static_cast<int>(HumanoidBone::Count), 0.0f);
            float maxScore = -1e9f;

            for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
                HumanoidBone b = static_cast<HumanoidBone>(i);
                scores[i] = ComputeScore(tokens, b, sample.relativeY, sample.relativeX, sample.depth, sample.parentIndex);
                if (scores[i] > maxScore) maxScore = scores[i];
            }

            // Softmax probabilities
            float sumExp = 0.0f;
            std::vector<float> probs(static_cast<int>(HumanoidBone::Count), 0.0f);
            for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
                probs[i] = std::exp(scores[i] - maxScore);
                sumExp += probs[i];
            }
            if (sumExp > 1e-6f) {
                for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
                    probs[i] /= sumExp;
                }
            }

            // Cross-entropy loss for target class
            if (targetId >= 0 && targetId < static_cast<int>(HumanoidBone::Count)) {
                float targetProb = (std::max)(probs[targetId], 1e-6f);
                epochLoss += -std::log(targetProb);

                // Prediction accuracy check
                int bestIdx = 0;
                float bestProb = probs[0];
                for (int i = 1; i < static_cast<int>(HumanoidBone::Count); ++i) {
                    if (probs[i] > bestProb) {
                        bestProb = probs[i];
                        bestIdx = i;
                    }
                }
                if (bestIdx == targetId) correctCount++;

                // Gradient update on token weights & biases
                for (int i = 0; i < static_cast<int>(HumanoidBone::Count); ++i) {
                    float grad = probs[i] - (i == targetId ? 1.0f : 0.0f);
                    m_boneBiases[static_cast<int16_t>(i)] -= learningRate * grad * 0.1f;

                    for (const auto& token : tokens) {
                        m_tokenWeights[token][static_cast<int16_t>(i)] -= learningRate * grad;
                    }
                }
            }
        }

        epochLoss /= static_cast<float>(m_dataset.size());
        if (epoch == 0) initialLoss = epochLoss;
        finalLoss = epochLoss;
    }

    metrics.initialLoss = initialLoss;
    metrics.finalLoss = finalLoss;
    metrics.accuracy = (static_cast<float>(correctCount) / static_cast<float>(m_dataset.size())) * 100.0f;
    metrics.vocabularySize = static_cast<int>(m_tokenWeights.size());

    m_lastMetrics = metrics;
    return metrics;
}

void BoneClassifierAI::ResetToDefaults() {
    m_tokenWeights.clear();
    m_boneBiases.clear();
    m_dataset.clear();
    SeedPretrainedWeights();
    SeedPretrainedDataset();
    m_lastMetrics = TrainMetrics{};
    SaveModel();
}

bool BoneClassifierAI::SaveModel(const std::string& path) {
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"vocabulary_size\": " << m_tokenWeights.size() << ",\n";
    file << "  \"dataset_size\": " << m_dataset.size() << ",\n";
    file << "  \"accuracy\": " << m_lastMetrics.accuracy << ",\n";

    // Write token weights
    file << "  \"weights\": [\n";
    size_t tokenIdx = 0;
    for (const auto& [token, boneMap] : m_tokenWeights) {
        file << "    {\"token\": \"" << token << "\", \"scores\": [";
        size_t scoreIdx = 0;
        for (const auto& [boneId, weight] : boneMap) {
            file << "{\"bone\": " << boneId << ", \"w\": " << weight << "}";
            if (++scoreIdx < boneMap.size()) file << ", ";
        }
        file << "]}";
        if (++tokenIdx < m_tokenWeights.size()) file << ",";
        file << "\n";
    }
    file << "  ],\n";

    // Write dataset
    file << "  \"samples\": [\n";
    for (size_t i = 0; i < m_dataset.size(); ++i) {
        const auto& s = m_dataset[i];
        file << "    {\"skel\": \"" << s.skeletonName << "\", \"name\": \"" << s.boneName
             << "\", \"bone\": " << static_cast<int>(s.targetBone)
             << ", \"parent\": " << s.parentIndex << ", \"y\": " << s.relativeY
             << ", \"x\": " << s.relativeX << ", \"depth\": " << s.depth << "}";
        if (i + 1 < m_dataset.size()) file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    return true;
}

bool BoneClassifierAI::LoadModelFromString(const std::string& jsonContent) {
    if (jsonContent.empty()) return false;
    std::istringstream file(jsonContent);

    std::string line;
    // Simple robust line-by-line parser for our JSON format
    bool inWeights = false;
    bool inSamples = false;

    while (std::getline(file, line)) {
        if (line.find("\"weights\":") != std::string::npos) {
            inWeights = true;
            inSamples = false;
            continue;
        }
        if (line.find("\"samples\":") != std::string::npos) {
            inWeights = false;
            inSamples = true;
            continue;
        }

        try {
            if (inWeights && line.find("\"token\":") != std::string::npos) {
                // Parse token
                size_t tokenPos = line.find("\"token\": \"");
                if (tokenPos != std::string::npos) {
                    tokenPos += 10;
                    size_t tokenEnd = line.find("\"", tokenPos);
                    if (tokenEnd != std::string::npos) {
                        std::string token = line.substr(tokenPos, tokenEnd - tokenPos);
                        
                        // Parse scores inside line
                        size_t pos = tokenEnd;
                        while ((pos = line.find("{\"bone\": ", pos)) != std::string::npos) {
                            pos += 9;
                            int boneId = std::stoi(line.substr(pos));
                            size_t wPos = line.find("\"w\": ", pos);
                            if (wPos != std::string::npos) {
                                wPos += 5;
                                float w = std::stof(line.substr(wPos));
                                m_tokenWeights[token][static_cast<int16_t>(boneId)] = w;
                            }
                        }
                    }
                }
            } else if (inSamples && line.find("\"skel\":") != std::string::npos) {
                BoneTrainingSample s;
                size_t skelPos = line.find("\"skel\": \"");
                if (skelPos != std::string::npos) {
                    skelPos += 9;
                    size_t skelEnd = line.find("\"", skelPos);
                    if (skelEnd != std::string::npos) s.skeletonName = line.substr(skelPos, skelEnd - skelPos);
                }
                size_t namePos = line.find("\"name\": \"");
                if (namePos != std::string::npos) {
                    namePos += 9;
                    size_t nameEnd = line.find("\"", namePos);
                    if (nameEnd != std::string::npos) s.boneName = line.substr(namePos, nameEnd - namePos);
                }
                size_t bonePos = line.find("\"bone\": ");
                if (bonePos != std::string::npos) {
                    s.targetBone = static_cast<HumanoidBone>(std::stoi(line.substr(bonePos + 8)));
                }
                size_t parentPos = line.find("\"parent\": ");
                if (parentPos != std::string::npos) {
                    s.parentIndex = static_cast<int16_t>(std::stoi(line.substr(parentPos + 10)));
                }
                size_t yPos = line.find("\"y\": ");
                if (yPos != std::string::npos) {
                    s.relativeY = std::stof(line.substr(yPos + 5));
                }
                size_t xPos = line.find("\"x\": ");
                if (xPos != std::string::npos) {
                    s.relativeX = std::stof(line.substr(xPos + 5));
                }
                size_t depthPos = line.find("\"depth\": ");
                if (depthPos != std::string::npos) {
                    s.depth = std::stoi(line.substr(depthPos + 9));
                }
                m_dataset.push_back(s);
            }
        } catch (...) {
            // Ignore malformed line
        }
    }

    return true;
}

bool BoneClassifierAI::LoadModel(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::stringstream ss;
    ss << file.rdbuf();
    return LoadModelFromString(ss.str());
}

} // namespace EngineAnim
