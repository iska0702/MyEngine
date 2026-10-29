#define NOMINMAX
#include "CharacterAsset.h"
#include "Ozz/OzzImporter.h"
#include "Ozz/OzzSerializer.h"
#include "ThirdParty/Libraries/ufbx.h"
#include "Platform/PlatformUtils.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace EngineAnim {

CharacterAsset::CharacterAsset() {
    InitializeDefaultSlots();
}

void CharacterAsset::InitializeDefaultSlots() {
    m_slots.clear();

    auto AddSlot = [this](const std::string& cat, const std::string& name) {
        AnimationSlot s;
        s.category = cat;
        s.name = name;
        s.status = "Not Assigned";
        s.isCustom = false;
        m_slots.push_back(s);
    };

    // LOCOMOTION
    AddSlot("LOCOMOTION", "Idle");
    AddSlot("LOCOMOTION", "Idle Variation");
    AddSlot("LOCOMOTION", "Walk Forward");
    AddSlot("LOCOMOTION", "Walk Backward");
    AddSlot("LOCOMOTION", "Walk Left");
    AddSlot("LOCOMOTION", "Walk Right");
    AddSlot("LOCOMOTION", "Run Forward");
    AddSlot("LOCOMOTION", "Run Backward");
    AddSlot("LOCOMOTION", "Run Left");
    AddSlot("LOCOMOTION", "Run Right");
    AddSlot("LOCOMOTION", "Sprint Forward");
    AddSlot("LOCOMOTION", "Sprint Backward");
    AddSlot("LOCOMOTION", "Sprint Left");
    AddSlot("LOCOMOTION", "Sprint Right");

    // STRAFE
    AddSlot("STRAFE", "Strafe Left");
    AddSlot("STRAFE", "Strafe Right");
    AddSlot("STRAFE", "Strafe Walk Left");
    AddSlot("STRAFE", "Strafe Walk Right");
    AddSlot("STRAFE", "Strafe Run Left");
    AddSlot("STRAFE", "Strafe Run Right");

    // START
    AddSlot("START", "Start Forward");
    AddSlot("START", "Start Backward");
    AddSlot("START", "Start Left");
    AddSlot("START", "Start Right");
    AddSlot("START", "Start Run Forward");
    AddSlot("START", "Start Run Backward");
    AddSlot("START", "Start Run Left");
    AddSlot("START", "Start Run Right");
    AddSlot("START", "Start Sprint Forward");
    AddSlot("START", "Start Sprint Backward");
    AddSlot("START", "Start Sprint Left");
    AddSlot("START", "Start Sprint Right");

    // STOP
    AddSlot("STOP", "Stop Forward");
    AddSlot("STOP", "Stop Backward");
    AddSlot("STOP", "Stop Left");
    AddSlot("STOP", "Stop Right");
    AddSlot("STOP", "Stop Run Forward");
    AddSlot("STOP", "Stop Run Backward");
    AddSlot("STOP", "Stop Run Left");
    AddSlot("STOP", "Stop Run Right");
    AddSlot("STOP", "Stop Sprint Forward");
    AddSlot("STOP", "Stop Sprint Backward");
    AddSlot("STOP", "Stop Sprint Left");
    AddSlot("STOP", "Stop Sprint Right");

    // TURNS
    AddSlot("TURNS", "Turn Left");
    AddSlot("TURNS", "Turn Right");
    AddSlot("TURNS", "Turn 45 Left");
    AddSlot("TURNS", "Turn 45 Right");
    AddSlot("TURNS", "Turn 90 Left");
    AddSlot("TURNS", "Turn 90 Right");
    AddSlot("TURNS", "Turn 180 Left");
    AddSlot("TURNS", "Turn 180 Right");
    AddSlot("TURNS", "Turn In Place Left");
    AddSlot("TURNS", "Turn In Place Right");

    // PIVOT
    AddSlot("PIVOT", "Pivot Left");
    AddSlot("PIVOT", "Pivot Right");
    AddSlot("PIVOT", "Pivot 45 Left");
    AddSlot("PIVOT", "Pivot 45 Right");
    AddSlot("PIVOT", "Pivot 90 Left");
    AddSlot("PIVOT", "Pivot 90 Right");
    AddSlot("PIVOT", "Pivot 180 Left");
    AddSlot("PIVOT", "Pivot 180 Right");

    // JUMP
    AddSlot("JUMP", "Jump Start");
    AddSlot("JUMP", "Jump Up");
    AddSlot("JUMP", "Jump Loop");
    AddSlot("JUMP", "Jump Land");
    AddSlot("JUMP", "Jump Forward");
    AddSlot("JUMP", "Jump Backward");
    AddSlot("JUMP", "Jump Left");
    AddSlot("JUMP", "Jump Right");

    // FALL
    AddSlot("FALL", "Fall Start");
    AddSlot("FALL", "Fall Loop");
    AddSlot("FALL", "Fall Land");
    AddSlot("FALL", "Fall Forward");
    AddSlot("FALL", "Fall Backward");
    AddSlot("FALL", "Fall Left");
    AddSlot("FALL", "Fall Right");

    // CROUCH
    AddSlot("CROUCH", "Crouch Idle");
    AddSlot("CROUCH", "Crouch Walk Forward");
    AddSlot("CROUCH", "Crouch Walk Backward");
    AddSlot("CROUCH", "Crouch Walk Left");
    AddSlot("CROUCH", "Crouch Walk Right");
    AddSlot("CROUCH", "Crouch Start");
    AddSlot("CROUCH", "Crouch End");

    // SLIDE
    AddSlot("SLIDE", "Slide Forward");
    AddSlot("SLIDE", "Slide Left");
    AddSlot("SLIDE", "Slide Right");
    AddSlot("SLIDE", "Slide Backward");
    AddSlot("SLIDE", "Slide Start");
    AddSlot("SLIDE", "Slide End");

    // DIRECTION CHANGES
    AddSlot("DIRECTION CHANGES", "Change Direction Left");
    AddSlot("DIRECTION CHANGES", "Change Direction Right");
    AddSlot("DIRECTION CHANGES", "Run -> Walk");
    AddSlot("DIRECTION CHANGES", "Walk -> Run");
    AddSlot("DIRECTION CHANGES", "Run -> Sprint");
    AddSlot("DIRECTION CHANGES", "Sprint -> Run");
    AddSlot("DIRECTION CHANGES", "Walk -> Stop");
    AddSlot("DIRECTION CHANGES", "Run -> Stop");
    AddSlot("DIRECTION CHANGES", "Sprint -> Stop");
}

std::shared_ptr<CharacterAsset> CharacterAsset::ImportFromFBX(const std::string& fbxPath, Renderer* renderer) {
    if (!renderer) return nullptr;

    std::filesystem::path srcPath(fbxPath);
    std::string charName = srcPath.stem().string();
    if (charName.empty()) charName = "Character";

    // 1. Copy FBX to Content/Characters/<name>/<name>.fbx
    std::string destDir = EnginePlatform::PlatformUtils::ResolveContentPath("Characters/" + charName);
    std::filesystem::create_directories(destDir);
    std::string destFbx = destDir + "/" + srcPath.filename().string();
    std::error_code ec;
    std::filesystem::copy_file(fbxPath, destFbx, std::filesystem::copy_options::overwrite_existing, ec);

    auto character = std::make_shared<CharacterAsset>();
    character->m_name = charName;
    character->m_sourceFbxPath = destFbx;

    // 2. Extract Skeleton
    std::string ozzSkelDir = EnginePlatform::PlatformUtils::ResolveContentPath("Ozz/Characters/" + charName);
    std::filesystem::create_directories(ozzSkelDir);
    std::string ozzSkelPath = ozzSkelDir + "/skeleton.ozz";

    if (!OzzImporter::ConvertFBXToOzzSkeleton(destFbx, ozzSkelPath, character->m_skeleton)) {
        return nullptr;
    }

    character->RunBoneRecognition();

    // 3. Extract Skinned Mesh with ufbx
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(destFbx.c_str(), &opts, &error);
    if (!scene) return nullptr;

    std::vector<SkinnedVertex> skinnedVerts;
    std::vector<uint32_t> indices;

    for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
        ufbx_mesh* mesh = scene->meshes.data[mi];
        size_t startVert = skinnedVerts.size();
        size_t numTris = mesh->num_triangles;
        uint32_t* triIndices = (uint32_t*)malloc(mesh->max_face_triangles * 3 * sizeof(uint32_t));

        ufbx_skin_deformer* skin = (mesh->skin_deformers.count > 0) ? mesh->skin_deformers.data[0] : nullptr;

        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            ufbx_face face = mesh->faces.data[fi];
            uint32_t numFaceTris = ufbx_triangulate_face(triIndices, mesh->max_face_triangles * 3, mesh, face);

            for (uint32_t ti = 0; ti < numFaceTris * 3; ++ti) {
                uint32_t index = triIndices[ti];
                SkinnedVertex sv{};

                ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                sv.position = Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));

                if (mesh->vertex_normal.exists) {
                    ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                    sv.normal = Vec3(static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
                } else {
                    sv.normal = Vec3(0, 1, 0);
                }

                if (mesh->vertex_uv.exists) {
                    ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                    sv.uv = Vec2(static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y));
                } else {
                    sv.uv = Vec2(0, 0);
                }

                // Bone skinning weights
                if (skin && index < skin->vertices.count) {
                    ufbx_skin_vertex skinVert = skin->vertices.data[index];
                    float totalWeight = 0.0f;
                    int numWeights = (std::min)(4, (int)skinVert.num_weights);

                    for (int w = 0; w < numWeights; ++w) {
                        ufbx_skin_weight sw = skin->weights.data[skinVert.weight_begin + w];
                        ufbx_skin_cluster* cluster = skin->clusters.data[sw.cluster_index];
                        if (cluster && cluster->bone_node) {
                            int boneIdx = character->m_skeleton.FindJointIndex(cluster->bone_node->name.data);
                            sv.boneIndices[w] = (boneIdx >= 0) ? (uint16_t)boneIdx : 0;
                            sv.boneWeights[w] = (float)sw.weight;
                            totalWeight += (float)sw.weight;
                        }
                    }
                    if (totalWeight > 1e-4f) {
                        for (int w = 0; w < numWeights; ++w) {
                            sv.boneWeights[w] /= totalWeight;
                        }
                    } else {
                        sv.boneWeights[0] = 1.0f;
                        sv.boneIndices[0] = 0;
                    }
                } else {
                    sv.boneWeights[0] = 1.0f;
                    sv.boneIndices[0] = 0;
                }

                skinnedVerts.push_back(sv);
                indices.push_back(static_cast<uint32_t>(skinnedVerts.size() - 1));
            }
        }
        free(triIndices);
    }
    ufbx_free_scene(scene);

    if (skinnedVerts.empty() || indices.empty()) return nullptr;

    character->m_mesh.SetData(std::move(skinnedVerts), std::move(indices), character->m_skeleton.GetInverseBindPoseMatrices());

    // 4. Register dynamic render model with Renderer
    XMFLOAT3 spawnPos = renderer->GetCamera() ? renderer->GetCamera()->GetPosition() : XMFLOAT3(0, 0, 0);
    spawnPos.y = 0.0f;

    Renderer::RenderDynamicModel dm;
    dm.name = charName;
    dm.filepath = destFbx;
    dm.indexCount = (UINT)character->m_mesh.GetIndexCount();
    dm.position = spawnPos;
    dm.texture = renderer->GetDefaultTexture();
    dm.material.preset = Renderer::MaterialPreset::Custom;
    dm.material.albedo = XMFLOAT3(0.85f, 0.85f, 0.85f);
    dm.material.roughness = 0.6f;
    dm.material.metallic = 0.0f;

#if defined(_WIN32)
    const UINT vertexBufferSize = (UINT)(character->m_mesh.GetVertexCount() * sizeof(Renderer::Vertex));
    const UINT indexBufferSize  = (UINT)(character->m_mesh.GetIndexCount()  * sizeof(uint32_t));

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Alignment = 0;
    vbDesc.Width = vertexBufferSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    auto uploadHeapProps = renderer->GetUploadHeapProps();

    if (FAILED(renderer->GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.vertexBuffer)))) {
        return nullptr;
    }

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = indexBufferSize;

    if (FAILED(renderer->GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.indexBuffer)))) {
        return nullptr;
    }

    // Upload index buffer
    void* pIdxData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (SUCCEEDED(dm.indexBuffer->Map(0, &readRange, &pIdxData)) && pIdxData) {
        std::memcpy(pIdxData, character->m_mesh.GetIndices().data(), indexBufferSize);
        dm.indexBuffer->Unmap(0, nullptr);
    }

    // Initial vertex deform & upload
    character->m_mesh.Deform(character->m_skeleton.GetBindPoseModelMatrices());
    character->m_mesh.UploadToVertexBuffer(dm.vertexBuffer.Get());

    dm.vertexBufferView.BufferLocation = dm.vertexBuffer->GetGPUVirtualAddress();
    dm.vertexBufferView.StrideInBytes = sizeof(Renderer::Vertex);
    dm.vertexBufferView.SizeInBytes = vertexBufferSize;

    dm.indexBufferView.BufferLocation = dm.indexBuffer->GetGPUVirtualAddress();
    dm.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    dm.indexBufferView.SizeInBytes = indexBufferSize;

    // Create Meshlets for DirectX 12 Mesh Shader Pipeline
    dm.meshletCount = ((UINT)character->m_mesh.GetIndexCount() / 3 + 31) / 32;
    std::vector<Renderer::Meshlet> meshlets(dm.meshletCount);
    for (UINT i = 0; i < dm.meshletCount; ++i) {
        meshlets[i].PrimitiveOffset = i * 32 * 3;
        meshlets[i].PrimitiveCount = (std::min)(32u, (UINT)(character->m_mesh.GetIndexCount() / 3 - i * 32));
        meshlets[i].VertexOffset = 0;
        meshlets[i].VertexCount = (UINT)character->m_mesh.GetVertexCount();
        meshlets[i].Radius = 5.0f;
        meshlets[i].LODError = 0.0f;
        meshlets[i].MaxParentError = 0.0f;
        meshlets[i].ParentID = 0;
    }

    UINT meshletBufferSize = (UINT)(meshlets.size() * sizeof(Renderer::Meshlet));
    D3D12_RESOURCE_DESC mbDesc = vbDesc;
    mbDesc.Width = meshletBufferSize;
    renderer->GetDevice()->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &mbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.meshletBuffer));

    void* pMbData = nullptr;
    if (dm.meshletBuffer && SUCCEEDED(dm.meshletBuffer->Map(0, &readRange, &pMbData)) && pMbData) {
        std::memcpy(pMbData, meshlets.data(), meshletBufferSize);
        dm.meshletBuffer->Unmap(0, nullptr);
    }

    auto& dynModels = renderer->GetDynamicModels();
    dynModels.push_back(std::move(dm));
    character->renderModelIndex = static_cast<int>(dynModels.size() - 1);
#else
    character->m_mesh.Deform(character->m_skeleton.GetBindPoseModelMatrices());
    uint32_t modelId = renderer->CreateDynamicClothModel(dm.name, character->m_mesh.GetDeformedVertices(), character->m_mesh.GetIndices(), dm.material);
    (void)modelId;
    character->renderModelIndex = static_cast<int>(renderer->GetDynamicModels().size() - 1);
#endif

    // 5. Setup Bone Mapping for Holden features
    MotionBoneMapping mapping;
    auto FindBone = [&](const std::vector<std::string>& candidates, int defaultIdx) -> int {
        for (const auto& c : candidates) {
            for (int i = 0; i < character->m_skeleton.GetNumJoints(); ++i) {
                std::string jName = character->m_skeleton.GetJoint(i).name;
                std::transform(jName.begin(), jName.end(), jName.begin(), ::tolower);
                std::string candLower = c;
                std::transform(candLower.begin(), candLower.end(), candLower.begin(), ::tolower);
                if (jName.find(candLower) != std::string::npos) {
                    return i;
                }
            }
        }
        return defaultIdx;
    };

    mapping.rootJoint = 0;
    mapping.hipsJoint = FindBone({"hips", "pelvis", "root"}, 0);
    mapping.leftFootJoint = FindBone({"leftfoot", "foot_l", "l_foot", "foot.l"}, (std::min)(4, character->m_skeleton.GetNumJoints() - 1));
    mapping.rightFootJoint = FindBone({"rightfoot", "foot_r", "r_foot", "foot.r"}, (std::min)(8, character->m_skeleton.GetNumJoints() - 1));
    mapping.leftHandJoint = FindBone({"lefthand", "hand_l", "l_hand", "hand.l"}, (std::min)(18, character->m_skeleton.GetNumJoints() - 1));
    mapping.rightHandJoint = FindBone({"righthand", "hand_r", "r_hand", "hand.r"}, (std::min)(22, character->m_skeleton.GetNumJoints() - 1));
    mapping.chestJoint = FindBone({"chest", "spine1", "spine_01", "spine"}, mapping.hipsJoint);
    mapping.headJoint = FindBone({"head"}, mapping.chestJoint);

    character->m_controller.boneMapping = mapping;
    character->m_controller.Initialize(&character->m_skeleton, &character->m_mesh, &character->m_motionDatabase);
    character->m_controller.worldPosition = Vec3(spawnPos.x, spawnPos.y, spawnPos.z);

    return character;
}

bool CharacterAsset::AssignFBXAnimation(int slotIndex, const std::string& fbxPath) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size())) return false;

    auto& slot = m_slots[slotIndex];
    slot.fbxPath = fbxPath;

    std::filesystem::path p(fbxPath);
    std::string safeName = p.stem().string();
    std::string ozzDir = EnginePlatform::PlatformUtils::ResolveContentPath("Ozz/Animations/" + m_name);
    std::filesystem::create_directories(ozzDir);
    slot.ozzPath = ozzDir + "/" + safeName + ".ozz";

    if (OzzImporter::ConvertFBXToOzzAnimation(fbxPath, m_skeleton, slot.ozzPath, slot.ozzAnimation)) {
        slot.status = "Ready";
        if (autoBuild) {
            BuildMotionMatching();
        }
        return true;
    } else {
        slot.status = "Error: Invalid FBX / No tracks";
        return false;
    }
}

bool CharacterAsset::AddCustomAnimation(const std::string& category, const std::string& name, const std::string& fbxPath) {
    AnimationSlot slot;
    slot.category = category.empty() ? "CUSTOM" : category;
    slot.name = name.empty() ? "Custom Animation" : name;
    slot.fbxPath = fbxPath;
    slot.isCustom = true;
    slot.status = "Not Assigned";

    m_slots.push_back(slot);
    int slotIndex = static_cast<int>(m_slots.size() - 1);

    if (!fbxPath.empty()) {
        return AssignFBXAnimation(slotIndex, fbxPath);
    }
    return true;
}

void CharacterAsset::RemoveCustomAnimation(int slotIndex) {
    if (slotIndex >= 0 && slotIndex < static_cast<int>(m_slots.size()) && m_slots[slotIndex].isCustom) {
        m_slots.erase(m_slots.begin() + slotIndex);
        if (autoBuild) {
            BuildMotionMatching();
        }
    }
}

bool CharacterAsset::BuildMotionMatching() {
    std::vector<OzzAnimation> readyAnims;
    for (const auto& slot : m_slots) {
        if (slot.status == "Ready" && slot.ozzAnimation.GetDuration() > 0.0f) {
            readyAnims.push_back(slot.ozzAnimation);
        }
    }

    if (readyAnims.empty()) {
        return false;
    }

    bool success = m_motionDatabase.BuildFromAnimations(
        m_skeleton,
        readyAnims,
        m_controller.boneMapping,
        m_controller.weights
    );

    if (success) {
        std::string dbDir = EnginePlatform::PlatformUtils::ResolveContentPath("Motion");
        std::filesystem::create_directories(dbDir);
        std::string dbPath = dbDir + "/" + m_name + ".motiondb";
        m_motionDatabase.Save(dbPath);
        m_controller.Initialize(&m_skeleton, &m_mesh, &m_motionDatabase);
    }

    return success;
}

void CharacterAsset::RunBoneRecognition() {
    m_humanoidBoneMapping = BoneClassifierAI::Get().ClassifySkeleton(m_skeleton);

    // Apply to motion matching controller
    int rootJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::Root);
    int hipsJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::Hips);
    int leftFootJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::LeftFoot);
    int rightFootJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::RightFoot);
    int leftHandJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::LeftHand);
    int rightHandJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::RightHand);
    int headJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::Head);
    int chestJoint = m_humanoidBoneMapping.GetJoint(HumanoidBone::Chest);

    if (rootJoint >= 0) m_controller.boneMapping.rootJoint = rootJoint;
    if (hipsJoint >= 0) m_controller.boneMapping.hipsJoint = hipsJoint;
    if (leftFootJoint >= 0) m_controller.boneMapping.leftFootJoint = leftFootJoint;
    if (rightFootJoint >= 0) m_controller.boneMapping.rightFootJoint = rightFootJoint;
    if (leftHandJoint >= 0) m_controller.boneMapping.leftHandJoint = leftHandJoint;
    if (rightHandJoint >= 0) m_controller.boneMapping.rightHandJoint = rightHandJoint;
    if (headJoint >= 0) m_controller.boneMapping.headJoint = headJoint;
    if (chestJoint >= 0) m_controller.boneMapping.chestJoint = chestJoint;
}

void CharacterAsset::PreviewPlay(int slotIndex) {
    if (slotIndex >= 0 && slotIndex < static_cast<int>(m_slots.size()) && m_slots[slotIndex].status == "Ready") {
        m_previewSlotIndex = slotIndex;
        m_isPreviewPlaying = true;
    }
}

void CharacterAsset::PreviewPause() {
    m_isPreviewPlaying = false;
}

void CharacterAsset::PreviewStop() {
    m_isPreviewPlaying = false;
    m_previewTime = 0.0f;
    m_previewSlotIndex = -1;
}

float CharacterAsset::GetPreviewDuration() const {
    if (m_previewSlotIndex >= 0 && m_previewSlotIndex < static_cast<int>(m_slots.size())) {
        return m_slots[m_previewSlotIndex].ozzAnimation.GetDuration();
    }
    return 0.0f;
}

void CharacterAsset::Update(float dt, Vec3 inputDir, bool isSprinting, bool isCrouching, bool isPlayMode, ID3D12Resource* pVertexBuffer) {
#if defined(_WIN32)
    if(ragdoll.Active()){m_mesh.Deform(ragdoll.Pose());if(pVertexBuffer)m_mesh.UploadToVertexBuffer(pVertexBuffer);return;}
#endif
    if (m_isPreviewPlaying && m_previewSlotIndex >= 0 && m_previewSlotIndex < static_cast<int>(m_slots.size())) {
        const auto& anim = m_slots[m_previewSlotIndex].ozzAnimation;
        float dur = anim.GetDuration();
        if (dur > 0.0f) {
            m_previewTime += dt * previewSpeed;
            if (m_previewTime > dur) {
                m_previewTime = std::fmod(m_previewTime, dur);
            }

            OzzSamplingJob sJob;
            sJob.skeleton = &m_skeleton;
            sJob.animation = &anim;
            sJob.time = m_previewTime;
            sJob.outputLocalTransforms = &m_previewPose;
            sJob.Run();

            OzzLocalToModelJob l2m;
            l2m.skeleton = &m_skeleton;
            l2m.inputLocalTransforms = &m_previewPose;
            l2m.outputModelMatrices = &m_previewModelMatrices;
            l2m.rootTransform = Transform::Identity();
            l2m.Run();

            m_mesh.Deform(m_previewModelMatrices);
            if (pVertexBuffer) {
                m_mesh.UploadToVertexBuffer(pVertexBuffer);
            }
        }
    } else if (motionMatchingEnabled && m_motionDatabase.GetNumFrames() > 0) {
        m_controller.Update(inputDir, isSprinting, isCrouching, dt, pVertexBuffer);
    } else {
        m_mesh.Deform(m_skeleton.GetBindPoseModelMatrices());
        if(pVertexBuffer)m_mesh.UploadToVertexBuffer(pVertexBuffer);
    }
}

} // namespace EngineAnim
