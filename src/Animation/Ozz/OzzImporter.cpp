#include "OzzImporter.h"
#include "OzzSerializer.h"
#include "ThirdParty/ufbx.h"
#include <iostream>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

namespace EngineAnim {

bool OzzImporter::ExtractSkeletonFromFBX(const std::string& fbxPath, OzzSkeleton& outSkeleton) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.load_external_files = false;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(fbxPath.c_str(), &opts, &error);
    if (!scene) {
        return false;
    }

    // Identify bone nodes (referenced by skins or marked as bone)
    std::unordered_set<const ufbx_node*> boneNodes;
    for (size_t si = 0; si < scene->skin_deformers.count; ++si) {
        ufbx_skin_deformer* skin = scene->skin_deformers.data[si];
        for (size_t ci = 0; ci < skin->clusters.count; ++ci) {
            ufbx_skin_cluster* cluster = skin->clusters.data[ci];
            if (cluster->bone_node) {
                boneNodes.insert(cluster->bone_node);
            }
        }
    }

    // If no skin deformers found, look for bone attribute
    if (boneNodes.empty()) {
        for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
            ufbx_node* node = scene->nodes.data[ni];
            if (node->bone != nullptr) {
                boneNodes.insert(node);
            }
        }
    }

    // Fallback: If still empty, take all non-root nodes
    if (boneNodes.empty()) {
        for (size_t ni = 1; ni < scene->nodes.count; ++ni) {
            boneNodes.insert(scene->nodes.data[ni]);
        }
    }

    // Include ancestors of bone nodes so hierarchy is unbroken
    std::vector<const ufbx_node*> orderedBones;
    std::unordered_set<const ufbx_node*> visited;

    std::function<void(const ufbx_node*)> addBoneAndAncestors = [&](const ufbx_node* node) {
        if (!node || node == scene->root_node || visited.count(node)) return;
        if (node->parent && node->parent != scene->root_node) {
            addBoneAndAncestors(node->parent);
        }
        if (!visited.count(node)) {
            visited.insert(node);
            orderedBones.push_back(node);
        }
    };

    for (const ufbx_node* bNode : boneNodes) {
        addBoneAndAncestors(bNode);
    }

    if (orderedBones.empty()) {
        ufbx_free_scene(scene);
        return false;
    }

    std::unordered_map<const ufbx_node*, int> nodeToIndex;
    for (int i = 0; i < static_cast<int>(orderedBones.size()); ++i) {
        nodeToIndex[orderedBones[i]] = i;
    }

    std::vector<OzzJoint> joints(orderedBones.size());
    for (int i = 0; i < static_cast<int>(orderedBones.size()); ++i) {
        const ufbx_node* node = orderedBones[i];
        joints[i].name = node->name.data ? std::string(node->name.data) : ("Bone_" + std::to_string(i));

        int16_t parentIdx = -1;
        if (node->parent && nodeToIndex.count(node->parent)) {
            parentIdx = static_cast<int16_t>(nodeToIndex[node->parent]);
        }
        joints[i].parentIndex = parentIdx;

        ufbx_vec3 t = node->local_transform.translation;
        ufbx_quat r = node->local_transform.rotation;
        ufbx_vec3 s = node->local_transform.scale;

        joints[i].restPose = Transform(
            Vec3(static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z)),
            Quat(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.z), static_cast<float>(r.w)),
            Vec3(static_cast<float>(s.x), static_cast<float>(s.y), static_cast<float>(s.z))
        );
    }

    outSkeleton.SetJoints(std::move(joints));
    ufbx_free_scene(scene);
    return true;
}

bool OzzImporter::ExtractAnimationFromFBX(const std::string& fbxPath, const OzzSkeleton& skeleton, OzzAnimation& outAnimation, float sampleRate) {
    if (skeleton.GetNumJoints() == 0) return false;

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.load_external_files = false;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(fbxPath.c_str(), &opts, &error);
    if (!scene) {
        return false;
    }

    if (scene->anim_stacks.count == 0) {
        ufbx_free_scene(scene);
        return false;
    }

    ufbx_anim_stack* animStack = scene->anim_stacks.data[0];
    ufbx_anim* anim = animStack->anim;
    double timeBegin = animStack->time_begin;
    double timeEnd = animStack->time_end;
    double duration = timeEnd - timeBegin;

    if (duration <= 0.0) {
        duration = 1.0;
    }

    int numFrames = static_cast<int>(std::ceil(duration * sampleRate)) + 1;
    if (numFrames < 2) numFrames = 2;
    double dt = duration / static_cast<double>(numFrames - 1);

    int numJoints = skeleton.GetNumJoints();
    std::vector<OzzJointTrack> tracks(numJoints);

    // Map skeleton joint names to scene nodes
    std::vector<ufbx_node*> jointNodes(numJoints, nullptr);
    for (int i = 0; i < numJoints; ++i) {
        const std::string& jName = skeleton.GetJoint(i).name;
        ufbx_node* node = ufbx_find_node(scene, jName.c_str());
        jointNodes[i] = node;
    }

    for (int frame = 0; frame < numFrames; ++frame) {
        double t = timeBegin + frame * dt;
        float outTime = static_cast<float>(frame * dt);

        ufbx_evaluate_opts evalOpts = {};
        ufbx_scene* evalScene = ufbx_evaluate_scene(scene, anim, t, &evalOpts, nullptr);
        if (!evalScene) continue;

        for (int i = 0; i < numJoints; ++i) {
            Transform jointTransform = skeleton.GetJoint(i).restPose;

            if (jointNodes[i]) {
                ufbx_node* evalNode = evalScene->nodes.data[jointNodes[i]->typed_id];
                if (evalNode) {
                    ufbx_vec3 tr = evalNode->local_transform.translation;
                    ufbx_quat rot = evalNode->local_transform.rotation;
                    ufbx_vec3 sc = evalNode->local_transform.scale;

                    jointTransform.translation = Vec3(static_cast<float>(tr.x), static_cast<float>(tr.y), static_cast<float>(tr.z));
                    jointTransform.rotation = Quat(static_cast<float>(rot.x), static_cast<float>(rot.y), static_cast<float>(rot.z), static_cast<float>(rot.w));
                    jointTransform.scale = Vec3(static_cast<float>(sc.x), static_cast<float>(sc.y), static_cast<float>(sc.z));
                }
            }

            OzzKeyframeTranslation kt;
            kt.time = outTime;
            kt.value = jointTransform.translation;
            tracks[i].translations.push_back(kt);

            OzzKeyframeRotation kr;
            kr.time = outTime;
            kr.value = jointTransform.rotation;
            tracks[i].rotations.push_back(kr);

            OzzKeyframeScale ks;
            ks.time = outTime;
            ks.value = jointTransform.scale;
            tracks[i].scales.push_back(ks);
        }

        ufbx_free_scene(evalScene);
    }

    outAnimation.SetName(animStack->name.data ? animStack->name.data : "Animation");
    outAnimation.SetDuration(static_cast<float>(duration));
    outAnimation.SetTracks(std::move(tracks));

    ufbx_free_scene(scene);
    return true;
}

bool OzzImporter::ConvertFBXToOzzSkeleton(const std::string& fbxPath, const std::string& outputOzzPath, OzzSkeleton& outSkeleton) {
    if (!ExtractSkeletonFromFBX(fbxPath, outSkeleton)) {
        return false;
    }
    return OzzSerializer::SaveSkeleton(outSkeleton, outputOzzPath);
}

bool OzzImporter::ConvertFBXToOzzAnimation(const std::string& fbxPath, const OzzSkeleton& skeleton, const std::string& outputOzzPath, OzzAnimation& outAnimation, float sampleRate) {
    if (!ExtractAnimationFromFBX(fbxPath, skeleton, outAnimation, sampleRate)) {
        return false;
    }
    return OzzSerializer::SaveAnimation(outAnimation, outputOzzPath);
}

} // namespace EngineAnim
