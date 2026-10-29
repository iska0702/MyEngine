#include "PrefabSystem.h"
#include "Asset/AssetRegistry.h"
#include "Asset/MaterialAsset.h"
#include "Platform/PlatformUtils.h"
#include "Renderer.h"
#include <filesystem>
#include <iostream>
#include <sstream>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EnginePrefab {

PrefabSystem& PrefabSystem::Get() {
    static PrefabSystem instance;
    return instance;
}

bool PrefabSystem::Initialize() {
    if (m_initialized) return true;
    m_initialized = true;

    std::cout << "[PrefabSystem] Initializing Prefab & Asset Registry..." << std::endl;

#if !defined(ENGINE_PLATFORM_PS4) && !defined(__PS4__) && !defined(__ORBIS__)
    std::string prefabsDir = EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs");

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(prefabsDir, ec);

    // Initialize Asset Registry pointing to Content directory
    EngineAsset::AssetRegistry::Get().Initialize(EnginePlatform::PlatformUtils::GetContentRoot());

    // Create default procedural starter prefabs if none exist
    CreateDefaultPrefabs();

    // Scan for all .prefab files
    ScanPrefabs(prefabsDir);
#else
    EngineAsset::AssetRegistry::Get().Initialize(EnginePlatform::PlatformUtils::GetContentRoot());
#endif

    std::cout << "[PrefabSystem] Initialized with " << m_prefabs.size() << " loaded prefabs." << std::endl;
    return true;
}

void PrefabSystem::Shutdown() {
    m_prefabs.clear();
    m_initialized = false;
}

void PrefabSystem::ScanPrefabs(const std::string& directory) {
#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    return;
#else
    std::string targetDir = directory.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs") : directory;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(targetDir, ec)) return;

    for (const auto& entry : fs::directory_iterator(targetDir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".prefab") {
            LoadPrefab(entry.path().generic_string());
        }
    }
#endif
}

std::shared_ptr<Prefab> PrefabSystem::LoadPrefab(const std::string& filepath) {
    namespace fs = std::filesystem;
    std::string filename = fs::path(filepath).stem().generic_string();

    auto prefab = std::make_shared<Prefab>(filename);
    if (prefab->LoadFromFile(filepath)) {
        m_prefabs[filename] = prefab;

        // Register in Asset Registry
        EngineAsset::AssetMetadata meta;
        meta.virtualPath = "prefabs/" + fs::path(filepath).filename().generic_string();
        meta.diskPath = filepath;
        meta.name = filename + ".prefab";
        meta.type = EngineAsset::AssetType::Prefab;
        meta.guid = prefab->GetGUID().empty() ? EngineAsset::AssetRegistry::GenerateGUID(meta.virtualPath) : prefab->GetGUID();
        std::error_code ec;
        meta.fileSize = fs::file_size(filepath, ec);
        meta.isLoaded = true;

        EngineAsset::AssetRegistry::Get().RegisterAsset(meta);
        return prefab;
    }
    return nullptr;
}

bool PrefabSystem::SavePrefab(const std::shared_ptr<Prefab>& prefab, const std::string& filepath) {
    if (!prefab) return false;
    std::string path = filepath.empty() ? prefab->GetFilePath() : filepath;
    if (path.empty()) {
        path = EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs/" + prefab->GetName() + ".prefab");
    }

    if (prefab->GetGUID().empty()) {
        prefab->SetGUID(EngineAsset::AssetRegistry::GenerateGUID(prefab->GetName()));
    }

    bool ok = prefab->SaveToFile(path);
    if (ok) {
        m_prefabs[prefab->GetName()] = prefab;

        namespace fs = std::filesystem;
        EngineAsset::AssetMetadata meta;
        meta.virtualPath = "prefabs/" + fs::path(path).filename().generic_string();
        meta.diskPath = path;
        meta.name = prefab->GetName() + ".prefab";
        meta.type = EngineAsset::AssetType::Prefab;
        meta.guid = prefab->GetGUID();
        std::error_code ec;
        meta.fileSize = fs::file_size(path, ec);
        meta.isLoaded = true;

        EngineAsset::AssetRegistry::Get().RegisterAsset(meta);
    }
    return ok;
}

std::shared_ptr<Prefab> PrefabSystem::GetPrefab(const std::string& name) const {
    auto it = m_prefabs.find(name);
    if (it != m_prefabs.end()) return it->second;
    return nullptr;
}

void PrefabSystem::CreateDefaultPrefabs() {
    namespace fs = std::filesystem;

    // 1. StreetLamp Prefab
    std::string streetLampPath = EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs/StreetLamp.prefab");
    if (!fs::exists(streetLampPath)) {
        auto lamp = std::make_shared<Prefab>("StreetLamp");
        lamp->SetGUID("c1a01b22-4e89-4f77-9a01-9a7b53cf1101");

        PrefabNodeData root;
        root.name = "StreetLamp_Root";
        root.tag = "Prop_Light";

        // Child 1: Lamp Post
        PrefabNodeData pole;
        pole.name = "Lamp_Pole";
        pole.localPosition = { 0.0f, 1.75f, 0.0f };
        pole.localScale = { 0.15f, 3.5f, 0.15f };
        pole.hasMesh = true;
        pole.meshType = "Cube";
        pole.diffuseColor = { 0.2f, 0.2f, 0.22f, 1.0f };
        pole.roughness = 0.6f;
        pole.metallic = 0.85f;
        root.children.push_back(pole);

        // Child 2: Lantern Cap
        PrefabNodeData cap;
        cap.name = "Lantern_Head";
        cap.localPosition = { 0.0f, 3.65f, 0.0f };
        cap.localScale = { 0.45f, 0.4f, 0.45f };
        cap.hasMesh = true;
        cap.meshType = "Cube";
        cap.diffuseColor = { 1.0f, 0.95f, 0.8f, 1.0f };
        cap.emissiveColor = { 1.0f, 0.85f, 0.5f };
        cap.emissiveIntensity = 4.0f;
        cap.roughness = 0.2f;

        // Light attached to Lantern Head
        cap.hasLight = true;
        cap.lightType = 0; // Point Light
        cap.lightColor = { 1.0f, 0.82f, 0.5f };
        cap.lightIntensity = 5.0f;
        cap.lightRadius = 14.0f;
        root.children.push_back(cap);

        lamp->SetRootData(root);
        SavePrefab(lamp, streetLampPath);
    }

    // 2. Campfire Prefab
    std::string campfirePath = EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs/Campfire.prefab");
    if (!fs::exists(campfirePath)) {
        auto fire = std::make_shared<Prefab>("Campfire");
        fire->SetGUID("c2b02c33-5f90-4e88-9b02-8b8c64da2202");

        PrefabNodeData root;
        root.name = "Campfire_Root";
        root.tag = "Prop_Fire";

        PrefabNodeData stones;
        stones.name = "StoneRing";
        stones.localPosition = { 0.0f, 0.1f, 0.0f };
        stones.localScale = { 1.2f, 0.2f, 1.2f };
        stones.hasMesh = true;
        stones.meshType = "Cube";
        stones.diffuseColor = { 0.35f, 0.33f, 0.3f, 1.0f };
        stones.roughness = 0.9f;
        root.children.push_back(stones);

        PrefabNodeData embers;
        embers.name = "Embers";
        embers.localPosition = { 0.0f, 0.25f, 0.0f };
        embers.localScale = { 0.5f, 0.25f, 0.5f };
        embers.hasMesh = true;
        embers.meshType = "Cube";
        embers.diffuseColor = { 1.0f, 0.4f, 0.1f, 1.0f };
        embers.emissiveColor = { 1.0f, 0.35f, 0.05f };
        embers.emissiveIntensity = 5.0f;
        embers.hasLight = true;
        embers.lightType = 0;
        embers.lightColor = { 1.0f, 0.45f, 0.15f };
        embers.lightIntensity = 4.5f;
        embers.lightRadius = 10.0f;
        root.children.push_back(embers);

        fire->SetRootData(root);
        SavePrefab(fire, campfirePath);
    }

    // 3. LootChest Prefab
    std::string chestPath = EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs/LootChest.prefab");
    if (!fs::exists(chestPath)) {
        auto chest = std::make_shared<Prefab>("LootChest");
        chest->SetGUID("c3c03d44-6a01-4f99-9c03-7c9d75eb3303");

        PrefabNodeData root;
        root.name = "LootChest_Root";
        root.tag = "Interactive_Chest";

        PrefabNodeData body;
        body.name = "Chest_Body";
        body.localPosition = { 0.0f, 0.35f, 0.0f };
        body.localScale = { 0.8f, 0.7f, 0.55f };
        body.hasMesh = true;
        body.meshType = "Cube";
        body.diffuseColor = { 0.5f, 0.28f, 0.12f, 1.0f };
        body.roughness = 0.65f;
        body.metallic = 0.15f;
        root.children.push_back(body);

        PrefabNodeData goldTrim;
        goldTrim.name = "Chest_Lock";
        goldTrim.localPosition = { 0.0f, 0.45f, 0.3f };
        goldTrim.localScale = { 0.15f, 0.2f, 0.08f };
        goldTrim.hasMesh = true;
        goldTrim.meshType = "Cube";
        goldTrim.diffuseColor = { 1.0f, 0.85f, 0.2f, 1.0f };
        goldTrim.emissiveColor = { 1.0f, 0.9f, 0.3f };
        goldTrim.emissiveIntensity = 2.0f;
        goldTrim.roughness = 0.2f;
        goldTrim.metallic = 0.95f;
        goldTrim.hasLight = true;
        goldTrim.lightType = 0;
        goldTrim.lightColor = { 1.0f, 0.85f, 0.25f };
        goldTrim.lightIntensity = 2.0f;
        goldTrim.lightRadius = 5.0f;
        root.children.push_back(goldTrim);

        chest->SetRootData(root);
        SavePrefab(chest, chestPath);
    }
}

void PrefabSystem::ExtractNodeData(EngineScene::SceneNode* node, PrefabNodeData& outData, Renderer* renderer) {
    if (!node) return;

    outData.name = node->GetName();
    outData.tag = node->GetTag();
    outData.localPosition = node->GetTransform().localPosition;
    outData.localRotation = node->GetTransform().localRotation;
    outData.localScale = node->GetTransform().localScale;

    // Check MeshComponent
    auto meshComp = node->GetComponent<EngineScene::MeshComponent>();
    if (meshComp) {
        outData.hasMesh = true;
        outData.meshType = "Cube"; // Default fallback

        if (renderer) {
            auto& models = renderer->GetModels();
            for (const auto& m : models) {
                if (m.modelId == meshComp->modelId || m.name == meshComp->modelName) {
                    outData.properties["model_settings"] = EngineAsset::EncodeModelAsset(m);
                    if (!m.geometryName.empty()) outData.meshType = m.geometryName;
                    outData.diffuseColor = XMFLOAT4(m.material.albedo.x, m.material.albedo.y, m.material.albedo.z, 1.0f);
                    outData.roughness = m.material.roughness;
                    outData.metallic = m.material.metallic;
                    outData.emissiveIntensity = m.material.emission;
                    outData.emissiveColor = XMFLOAT3(m.material.albedo.x, m.material.albedo.y, m.material.albedo.z);
                    break;
                }
            }
        }
    }

    // Check Lights
    auto plComp = node->GetComponent<EngineScene::PointLightComponent>();
    if (plComp && renderer) {
        auto& plights = renderer->GetPointLights();
        if (plComp->lightIndex >= 0 && plComp->lightIndex < (int)plights.size()) {
            const auto& pl = plights[plComp->lightIndex];
            outData.hasLight = true;
            outData.lightType = 0;
            outData.lightColor = pl.color;
            outData.lightIntensity = pl.intensity;
            outData.lightRadius = pl.range;
            outData.lightOffset = plComp->relativeOffset;
        }
    }

    auto slComp = node->GetComponent<EngineScene::SpotLightComponent>();
    if (slComp && renderer) {
        auto& slights = renderer->GetSpotLights();
        if (slComp->lightIndex >= 0 && slComp->lightIndex < (int)slights.size()) {
            const auto& sl = slights[slComp->lightIndex];
            outData.hasLight = true;
            outData.lightType = 1;
            outData.lightColor = sl.color;
            outData.lightIntensity = sl.intensity;
            outData.lightRadius = sl.range;
            outData.spotAngle = sl.outerAngle;
            outData.lightOffset = slComp->relativeOffset;
        }
    }

    // Check Audio
    auto audioComp = node->GetComponent<EngineScene::AudioEmitterComponent>();
    if (audioComp) {
        outData.hasAudio = true;
        outData.audioPath = audioComp->soundPath;
        outData.audioVolume = audioComp->volume;
        outData.audioMaxDist = audioComp->maxDistance;
        outData.audioLoop = audioComp->loop;
    }

    // Children
    for (const auto& child : node->GetChildren()) {
        if (child) {
            PrefabNodeData childData;
            ExtractNodeData(child.get(), childData, renderer);
            outData.children.push_back(childData);
        }
    }
}

std::shared_ptr<Prefab> PrefabSystem::CreatePrefabFromNode(
    EngineScene::SceneNode* node,
    const std::string& prefabName,
    Renderer* renderer,
    const std::string& saveDirectory
) {
    if (!node) return nullptr;

    auto prefab = std::make_shared<Prefab>(prefabName);
    prefab->SetGUID(EngineAsset::AssetRegistry::GenerateGUID(prefabName));

    PrefabNodeData rootData;
    ExtractNodeData(node, rootData, renderer);
    prefab->SetRootData(rootData);

    std::string dir = saveDirectory.empty() ? EnginePlatform::PlatformUtils::ResolveContentPath("Prefabs") : saveDirectory;
    std::string savePath = dir + "/" + prefabName + ".prefab";
    SavePrefab(prefab, savePath);

    return prefab;
}

std::shared_ptr<EngineScene::SceneNode> PrefabSystem::BuildNodeHierarchy(
    const PrefabNodeData& data,
    std::shared_ptr<EngineScene::SceneNode> parent,
    Renderer* renderer,
    const std::string& prefabName
) {
    auto node = EngineScene::SceneGraph::Get().CreateNode(data.name, parent);
    if (!node) return nullptr;

    node->SetTag(data.tag);
    node->GetTransform().localPosition = data.localPosition;
    node->GetTransform().localRotation = data.localRotation;
    node->GetTransform().localScale = data.localScale;
    node->GetTransform().Update(parent ? XMLoadFloat4x4(&parent->GetTransform().worldMatrix) : XMMatrixIdentity());

    // Spawn Mesh if needed
    if (data.hasMesh && renderer) {
        XMFLOAT3 spawnWorldPos = node->GetTransform().GetWorldPosition();

        if (data.meshType == "Plane") {
            renderer->SpawnPlane(spawnWorldPos);
        } else if (data.meshType == "Sphere") {
            renderer->SpawnSphere(spawnWorldPos);
        } else {
            renderer->SpawnCube(spawnWorldPos);
        }

        // Apply material to newly spawned model
        auto& models = renderer->GetModels();
        if (!models.empty()) {
            auto& m = models.back();
            m.material.albedo = XMFLOAT3(data.diffuseColor.x, data.diffuseColor.y, data.diffuseColor.z);
            m.material.roughness = data.roughness;
            m.material.metallic = data.metallic;
            m.material.emission = data.emissiveIntensity;
            const auto settings = data.properties.find("model_settings");
            if (settings != data.properties.end() && !EngineAsset::DecodeModelAsset(settings->second, m, renderer))
                std::cerr << "[PrefabSystem] Invalid model settings for " << data.name << std::endl;
            m.prefabSource = prefabName;
            EngineScene::Transform::Decompose(XMLoadFloat4x4(&node->GetTransform().worldMatrix), m.position, m.rotation, m.scale);

            node->AddComponent<EngineScene::MeshComponent>(m.modelId, false, m.name);
        }
    }

    // Spawn Light if needed
    if (data.hasLight && renderer) {
        if (data.lightType == 0) { // Point Light
            EngineRender::PointLight pl;
            pl.name = data.name + "_Light";
            pl.position = node->GetTransform().GetWorldPosition();
            pl.color = data.lightColor;
            pl.intensity = data.lightIntensity;
            pl.range = data.lightRadius;
            pl.enabled = true;

            renderer->AddPointLight(pl);
            int lightIdx = static_cast<int>(renderer->GetPointLights().size()) - 1;
            auto comp = node->AddComponent<EngineScene::PointLightComponent>(lightIdx);
            comp->relativeOffset = data.lightOffset;
        } else { // Spot Light
            EngineRender::SpotLight sl;
            sl.name = data.name + "_Spot";
            sl.position = node->GetTransform().GetWorldPosition();
            sl.color = data.lightColor;
            sl.intensity = data.lightIntensity;
            sl.range = data.lightRadius;
            sl.outerAngle = data.spotAngle;
            sl.innerAngle = data.spotAngle * 0.6f;
            sl.enabled = true;

            renderer->AddSpotLight(sl);
            int lightIdx = static_cast<int>(renderer->GetSpotLights().size()) - 1;
            auto comp = node->AddComponent<EngineScene::SpotLightComponent>(lightIdx);
            comp->relativeOffset = data.lightOffset;
        }
    }

    // Audio Emitter Component
    if (data.hasAudio) {
        auto audioComp = node->AddComponent<EngineScene::AudioEmitterComponent>();
        audioComp->soundPath = data.audioPath;
        audioComp->volume = data.audioVolume;
        audioComp->maxDistance = data.audioMaxDist;
        audioComp->loop = data.audioLoop;
    }

    // Recursively build children
    for (const auto& childData : data.children) {
        BuildNodeHierarchy(childData, node, renderer, prefabName);
    }

    return node;
}

std::shared_ptr<EngineScene::SceneNode> PrefabSystem::Instantiate(
    const std::string& prefabName,
    std::shared_ptr<EngineScene::SceneNode> parent,
    const DirectX::XMFLOAT3& worldPos,
    const DirectX::XMFLOAT3& worldRot,
    const DirectX::XMFLOAT3& worldScale
) {
    return InstantiateWithRenderer(prefabName, nullptr, parent, worldPos, worldRot, worldScale);
}

std::shared_ptr<EngineScene::SceneNode> PrefabSystem::InstantiateWithRenderer(
    const std::string& prefabName,
    Renderer* renderer,
    std::shared_ptr<EngineScene::SceneNode> parent,
    const DirectX::XMFLOAT3& worldPos,
    const DirectX::XMFLOAT3& worldRot,
    const DirectX::XMFLOAT3& worldScale
) {
    auto prefab = GetPrefab(prefabName);
    if (!prefab) {
        std::cerr << "[PrefabSystem] Failed to find prefab: " << prefabName << std::endl;
        return nullptr;
    }

    auto rootData = prefab->GetRootData();
    rootData.localPosition = worldPos;
    rootData.localRotation = worldRot;
    rootData.localScale = {rootData.localScale.x * worldScale.x, rootData.localScale.y * worldScale.y, rootData.localScale.z * worldScale.z};
    auto rootNode = BuildNodeHierarchy(rootData, parent, renderer, prefabName);
    if (!rootNode) return nullptr;

    std::cout << "[PrefabSystem] Instantiated prefab '" << prefabName << "' at ("
              << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")" << std::endl;

    return rootNode;
}

void PrefabSystem::DrawUI(Renderer* renderer, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(850, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Prefab & Asset Registry Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("PrefabStudioTabs")) {

        // -------------------------------------------------------------
        // TAB 1: PREFAB LIBRARY
        // -------------------------------------------------------------
        if (ImGui::BeginTabItem(" Prefab Library")) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Active Loaded Prefabs: %zu", m_prefabs.size());
            ImGui::SameLine();
            if (ImGui::Button(" Rescan Prefabs")) {
                ScanPrefabs();
            }

            ImGui::Separator();

            float listWidth = 260.0f;
            ImGui::BeginChild("PrefabListPane", ImVec2(listWidth, 0), true);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Available Archetypes");
            ImGui::Separator();

            for (const auto& pair : m_prefabs) {
                bool isSelected = (m_selectedPrefabName == pair.first);
                std::string label = " " + pair.first;
                if (ImGui::Selectable(label.c_str(), isSelected)) {
                    m_selectedPrefabName = pair.first;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Prefab Inspector & Spawn Pane
            ImGui::BeginChild("PrefabInspectorPane", ImVec2(0, 0), true);
            auto selectedPrefab = GetPrefab(m_selectedPrefabName);
            if (selectedPrefab) {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Prefab: %s", selectedPrefab->GetName().c_str());
                ImGui::Text("File: %s", selectedPrefab->GetFilePath().c_str());
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GUID: %s", selectedPrefab->GetGUID().c_str());
                ImGui::Separator();

                // Spawning Actions
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Spawn in World:");
                
                // Spawn 3 meters in front of camera
                if (ImGui::Button(" Spawn in Front of Camera")) {
                    DirectX::XMFLOAT3 spawnPos = {
                        cameraPos.x + cameraForward.x * 3.0f,
                        cameraPos.y + cameraForward.y * 3.0f,
                        cameraPos.z + cameraForward.z * 3.0f
                    };
                    InstantiateWithRenderer(selectedPrefab->GetName(), renderer, nullptr, spawnPos);
                }
                ImGui::SameLine();
                // Spawn on ground floor at camera X,Z
                if (ImGui::Button(" Spawn on Ground (Y=0)")) {
                    DirectX::XMFLOAT3 spawnPos = { cameraPos.x + cameraForward.x * 2.0f, 0.0f, cameraPos.z + cameraForward.z * 2.0f };
                    InstantiateWithRenderer(selectedPrefab->GetName(), renderer, nullptr, spawnPos);
                }

                ImGui::Separator();
                ImGui::Text("Node Structure:");
                const auto& root = selectedPrefab->GetRootData();
                ImGui::BulletText("%s (Tag: %s, Children: %zu)", root.name.c_str(), root.tag.c_str(), root.children.size());
                for (const auto& child : root.children) {
                    ImGui::Indent();
                    ImGui::BulletText("%s [Mesh: %s, Light: %s]",
                        child.name.c_str(),
                        child.hasMesh ? child.meshType.c_str() : "None",
                        child.hasLight ? (child.lightType == 0 ? "Point" : "Spot") : "None");
                    ImGui::Unindent();
                }
            } else {
                ImGui::TextDisabled("Select a prefab archetype on the left to inspect or spawn.");
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        // -------------------------------------------------------------
        // TAB 2: ASSET REGISTRY
        // -------------------------------------------------------------
        if (ImGui::BeginTabItem(" Asset Registry")) {
            EngineAsset::AssetRegistry::Get().DrawUI();
            ImGui::EndTabItem();
        }

        // -------------------------------------------------------------
        // TAB 3: CREATE PREFAB FROM LIVE SCENE
        // -------------------------------------------------------------
        if (ImGui::BeginTabItem(" Create Prefab")) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Export Scene Node as Reusable Prefab");
            ImGui::Separator();

            ImGui::InputText("Prefab Name", m_newPrefabName, sizeof(m_newPrefabName));

            auto rootNode = EngineScene::SceneGraph::Get().GetRoot();
            if (rootNode) {
                ImGui::Text("Select Source Scene Node:");
                if (ImGui::BeginCombo("Scene Node", rootNode->GetName().c_str())) {
                    for (const auto& child : rootNode->GetChildren()) {
                        if (child) {
                            bool isSel = (m_selectedSceneNodeId == child->GetId());
                            if (ImGui::Selectable(child->GetName().c_str(), isSel)) {
                                m_selectedSceneNodeId = child->GetId();
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                auto targetNode = EngineScene::SceneGraph::Get().FindNodeById(m_selectedSceneNodeId);
                if (!targetNode) targetNode = rootNode;

                ImGui::Spacing();
                if (ImGui::Button(" Create & Save Prefab (.prefab)", ImVec2(250, 32))) {
                    std::string pName = m_newPrefabName;
                    if (!pName.empty() && targetNode) {
                        CreatePrefabFromNode(targetNode.get(), pName, renderer);
                        m_selectedPrefabName = pName;
                    }
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
#endif
}

} // namespace EnginePrefab
