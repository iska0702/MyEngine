#include "SceneGraph.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include <algorithm>
#include <iostream>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineScene {

SceneGraph& SceneGraph::Get() {
    static SceneGraph s_instance;
    return s_instance;
}

SceneGraph::SceneGraph() {
    Initialize();
}

void SceneGraph::Initialize() {
    if (!m_root) {
        m_root = std::make_shared<SceneNode>("SceneRoot", 0);
        m_allNodes[0] = m_root;
    }
}

void SceneGraph::Shutdown() {
    Clear();
    m_root.reset();
}

void SceneGraph::Clear() {
    m_allNodes.clear();
    m_selectedNode.reset();
    if (m_root) {
        while (!m_root->GetChildren().empty()) {
            m_root->RemoveChild(m_root->GetChildren().front()->GetId());
        }
        m_allNodes[0] = m_root;
    }
}

std::shared_ptr<SceneNode> SceneGraph::CreateNode(const std::string& name, std::shared_ptr<SceneNode> parent) {
    if (!m_root) Initialize();

    auto node = std::make_shared<SceneNode>(name);
    m_allNodes[node->GetId()] = node;

    if (parent) {
        parent->AddChild(node);
    } else {
        m_root->AddChild(node);
    }

    return node;
}

bool SceneGraph::RemoveNode(uint32_t nodeId) {
    if (nodeId == 0) return false; // Cannot remove root

    auto it = m_allNodes.find(nodeId);
    if (it == m_allNodes.end()) return false;

    auto node = it->second;
    if (node->GetParent()) {
        node->GetParent()->RemoveChild(nodeId);
    }

    if (m_selectedNode == node) {
        m_selectedNode.reset();
    }

    // Recursively unregister children
    const auto children = node->GetChildren();
    for (const auto& child : children) {
        if (child) RemoveNode(child->GetId());
    }

    m_allNodes.erase(it);
    return true;
}

std::shared_ptr<SceneNode> SceneGraph::FindNode(const std::string& name) const {
    for (const auto& pair : m_allNodes) {
        if (pair.second && pair.second->GetName() == name) {
            return pair.second;
        }
    }
    return nullptr;
}

std::shared_ptr<SceneNode> SceneGraph::FindNodeById(uint32_t nodeId) const {
    auto it = m_allNodes.find(nodeId);
    if (it != m_allNodes.end()) {
        return it->second;
    }
    return nullptr;
}

void SceneGraph::Update(float dt) {
    if (!m_root) return;

    // Demo rotation animation
    if (m_demoRotating) {
        m_demoRotationAngle += dt * 45.0f; // 45 degrees per sec
        if (m_demoRotationAngle >= 360.0f) m_demoRotationAngle -= 360.0f;

        auto demoParent = FindNode("Drone_Platform_Base");
        if (demoParent) {
            demoParent->GetTransform().localRotation.y = m_demoRotationAngle;
        }

        auto radarChild = FindNode("Drone_Turret_Radar");
        if (radarChild) {
            radarChild->GetTransform().localRotation.y = m_demoRotationAngle * 2.5f;
        }
    }

    // Hierarchical transform update from root down to all leaves
    m_root->UpdateHierarchy(XMMatrixIdentity(), dt);
}

void SceneGraph::SyncNodeFromModel(Renderer* renderer,uint32_t modelId){
    if(!renderer)return;
    for(auto& entry:m_allNodes){auto node=entry.second;auto c=node->GetComponent<MeshComponent>();if(!c||c->modelId!=modelId)continue;
        auto copy=[&](auto& models){for(auto& m:models)if(m.modelId==modelId){
            XMMATRIX world=XMMatrixScaling(m.scale.x,m.scale.y,m.scale.z)*XMMatrixRotationRollPitchYaw(XMConvertToRadians(m.rotation.x),XMConvertToRadians(m.rotation.y),XMConvertToRadians(m.rotation.z))*XMMatrixTranslation(m.position.x,m.position.y,m.position.z);
            XMMATRIX parent=node->GetParent()?XMLoadFloat4x4(&node->GetParent()->GetTransform().worldMatrix):XMMatrixIdentity();
            auto& t=node->GetTransform();Transform::Decompose(world*XMMatrixInverse(nullptr,parent),t.localPosition,t.localRotation,t.localScale);t.isDirty=true;
        }};if(c->isDynamic)copy(renderer->GetDynamicModels());else copy(renderer->GetModels());
    }
}

void SceneGraph::SyncWithRenderer(Renderer* renderer,bool force) {
    if (!renderer) return;

    auto& models = renderer->GetModels();
    auto& dynModels = renderer->GetDynamicModels();
    auto& pointLights = renderer->GetPointLights();
    auto& spotLights = renderer->GetSpotLights();

    for (const auto& pair : m_allNodes) {
        auto node = pair.second;
        if (!node || !node->IsActive()) continue;

        const auto& wMat = node->GetTransform().worldMatrix;
        XMFLOAT3 wPos = node->GetTransform().GetWorldPosition();
        XMFLOAT3 wFwd = node->GetTransform().GetWorldForward();

        for (const auto& comp : node->GetComponents()) {
            if (auto meshComp = std::dynamic_pointer_cast<MeshComponent>(comp)) {
                if (meshComp->isDynamic) {
                    for (auto& dm : dynModels) {
                        if (dm.modelId == meshComp->modelId || dm.name == meshComp->modelName) {
                            if(!force&&(dm.physicsBodyType==2||dm.gameplay.door))break;
                            dm.worldMatrix = wMat;
                            Transform::Decompose(XMLoadFloat4x4(&wMat),dm.position,dm.rotation,dm.scale);
                            break;
                        }
                    }
                } else {
                    for (auto& m : models) {
                        if (m.modelId == meshComp->modelId || m.name == meshComp->modelName) {
                            if(!force&&(m.physicsBodyType==2||m.gameplay.door))break;
                            m.worldMatrix = wMat;
                            Transform::Decompose(XMLoadFloat4x4(&wMat),m.position,m.rotation,m.scale);
                            break;
                        }
                    }
                }
            } else if (auto plComp = std::dynamic_pointer_cast<PointLightComponent>(comp)) {
                if (plComp->lightIndex >= 0 && plComp->lightIndex < (int)pointLights.size()) {
                    pointLights[plComp->lightIndex].position = XMFLOAT3(
                        wPos.x + plComp->relativeOffset.x,
                        wPos.y + plComp->relativeOffset.y,
                        wPos.z + plComp->relativeOffset.z
                    );
                }
            } else if (auto slComp = std::dynamic_pointer_cast<SpotLightComponent>(comp)) {
                if (slComp->lightIndex >= 0 && slComp->lightIndex < (int)spotLights.size()) {
                    spotLights[slComp->lightIndex].position = XMFLOAT3(
                        wPos.x + slComp->relativeOffset.x,
                        wPos.y + slComp->relativeOffset.y,
                        wPos.z + slComp->relativeOffset.z
                    );
                    spotLights[slComp->lightIndex].direction = wFwd;
                }
            }
        }
    }
}

void SceneGraph::CreateDemoHierarchy(Renderer* renderer) {
    // 1. Create Parent Node: "Drone_Platform_Base"
    auto baseNode = CreateNode("Drone_Platform_Base");
    baseNode->GetTransform().localPosition = XMFLOAT3(0.0f, 3.5f, 0.0f);
    baseNode->GetTransform().localScale = XMFLOAT3(1.0f, 0.4f, 1.0f);
    baseNode->AddSocket("Turret_Socket", XMFLOAT3(0.0f, 0.4f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f));
    baseNode->AddSocket("Flashlight_Socket", XMFLOAT3(0.0f, -0.2f, 0.8f), XMFLOAT3(15.0f, 0.0f, 0.0f));
    baseNode->AddSocket("Beacon_Socket", XMFLOAT3(0.0f, 0.8f, 0.0f), XMFLOAT3(0.0f, 0.0f, 0.0f));

    // 2. Child 1: Radar Turret on Turret_Socket
    auto turretNode = CreateNode("Drone_Turret_Radar");
    turretNode->AttachToSocket(baseNode.get(), "Turret_Socket");
    turretNode->GetTransform().localScale = XMFLOAT3(0.4f, 0.6f, 0.4f);

    // 3. Child 2: Flashlight SpotLight attached to Flashlight_Socket
    if (renderer) {
        EngineRender::SpotLight droneSpot;
        droneSpot.name = "Drone Searchlight";
        droneSpot.color = XMFLOAT3(0.9f, 0.95f, 1.0f);
        droneSpot.intensity = 8.0f;
        droneSpot.range = 30.0f;
        droneSpot.innerAngle = 18.0f;
        droneSpot.outerAngle = 32.0f;
        droneSpot.enabled = true;
        renderer->AddSpotLight(droneSpot);
        int spotIdx = (int)renderer->GetSpotLights().size() - 1;

        auto lightNode = CreateNode("Drone_Searchlight_Mount");
        lightNode->AttachToSocket(baseNode.get(), "Flashlight_Socket");
        lightNode->AddComponent<SpotLightComponent>(spotIdx);

        // 4. Child 3: Beacon PointLight attached to Beacon_Socket
        EngineRender::PointLight dronePoint;
        dronePoint.name = "Drone Warning Beacon";
        dronePoint.color = XMFLOAT3(1.0f, 0.2f, 0.1f);
        dronePoint.intensity = 4.5f;
        dronePoint.range = 14.0f;
        dronePoint.enabled = true;
        renderer->AddPointLight(dronePoint);
        int pointIdx = (int)renderer->GetPointLights().size() - 1;

        auto beaconNode = CreateNode("Drone_Beacon_Mount");
        beaconNode->AttachToSocket(baseNode.get(), "Beacon_Socket");
        beaconNode->AddComponent<PointLightComponent>(pointIdx);
    }

    m_selectedNode = baseNode;
}

void SceneGraph::DrawUI(Renderer* renderer, Camera* camera) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Scene Graph & Transform Hierarchy", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    // Top action bar
    if (ImGui::Button(" Add Root Node")) {
        auto node = CreateNode("New_Entity_" + std::to_string(m_allNodes.size()));
        m_selectedNode = node;
    }
    ImGui::SameLine();
    if (ImGui::Button(" Spawn Demo Drone Hierarchy")) {
        CreateDemoHierarchy(renderer);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Rotate Demo", &m_demoRotating);

    ImGui::Separator();

    // Two-panel layout: Left = Hierarchy Tree, Right = Selected Node Inspector
    ImGui::BeginChild("HierarchyTreePanel", ImVec2(240, 0), true);
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Scene Tree:");
    ImGui::Separator();

    if (m_root) {
        DrawNodeHierarchyTree(m_root);
    }

    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("NodeInspectorPanel", ImVec2(0, 0), true);
    DrawNodeInspector(renderer, camera);
    ImGui::EndChild();

    ImGui::End();
#else
    (void)renderer;
    (void)camera;
#endif
}

void SceneGraph::DrawNodeHierarchyTree(const std::shared_ptr<SceneNode>& node) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!node) return;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (node == m_selectedNode) flags |= ImGuiTreeNodeFlags_Selected;
    if (node->GetChildren().empty()) flags |= ImGuiTreeNodeFlags_Leaf;

    std::string icon = " ";
    if (node->GetId() == 0) icon = " ";
    else if (node->HasComponent<SpotLightComponent>()) icon = " ";
    else if (node->HasComponent<PointLightComponent>()) icon = " ";
    else if (node->HasComponent<MeshComponent>()) icon = " ";
    else if (node->HasComponent<AudioEmitterComponent>()) icon = " ";

    std::string label = icon + node->GetName();
    bool nodeOpen = ImGui::TreeNodeEx((void*)(uintptr_t)node->GetId(), flags, "%s", label.c_str());

    if (ImGui::IsItemClicked()) {
        m_selectedNode = node;
    }

    // Context Menu on right-click
    if (ImGui::BeginPopupContextItem()) {
        m_selectedNode = node;
        if (ImGui::MenuItem(" Add Child Node")) {
            auto child = CreateNode("Child_" + std::to_string(m_allNodes.size()), node);
            m_selectedNode = child;
        }
        if (node->GetId() != 0) {
            if (ImGui::MenuItem(" Delete Node")) {
                RemoveNode(node->GetId());
                ImGui::EndPopup();
                if (nodeOpen) ImGui::TreePop();
                return;
            }
        }
        ImGui::EndPopup();
    }

    if (nodeOpen) {
        for (const auto& child : node->GetChildren()) {
            DrawNodeHierarchyTree(child);
        }
        ImGui::TreePop();
    }
#endif
}

void SceneGraph::DrawNodeInspector(Renderer* renderer, Camera* camera) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_selectedNode) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select a node in the tree to inspect properties.");
        return;
    }

    auto node = m_selectedNode;
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Node: %s (ID: %u)", node->GetName().c_str(), node->GetId());
    ImGui::Separator();

    // Name & Active
    char nameBuf[128];
    strncpy_s(nameBuf, node->GetName().c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        node->SetName(nameBuf);
    }

    bool active = node->IsActive();
    if (ImGui::Checkbox("Active", &active)) {
        node->SetActive(active);
    }

    // Parent Selector Combo
    SceneNode* currentParent = node->GetParent();
    std::string parentName = currentParent ? currentParent->GetName() : "None (Root)";
    if (node->GetId() != 0) {
        if (ImGui::BeginCombo("Parent", parentName.c_str())) {
            for (const auto& pair : m_allNodes) {
                auto candidate = pair.second;
                if (candidate && candidate != node) {
                    bool isSelected = (candidate.get() == currentParent);
                    if (ImGui::Selectable(candidate->GetName().c_str(), isSelected)) {
                        node->SetParent(candidate.get(), true);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Separator();

    // Transform Section
    if (ImGui::CollapsingHeader("Transform (Local)", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& t = node->GetTransform();
        ImGui::DragFloat3("Position", &t.localPosition.x, 0.1f);
        if (camera) {
            ImGui::SameLine();
            if (ImGui::Button(" To Cam")) {
                t.localPosition = camera->GetPosition();
            }
        }
        ImGui::DragFloat3("Rotation (deg)", &t.localRotation.x, 0.5f);
        ImGui::DragFloat3("Scale", &t.localScale.x, 0.05f, 0.01f, 50.0f);

        // Readout World Transform
        XMFLOAT3 wPos = t.GetWorldPosition();
        XMFLOAT3 wFwd = t.GetWorldForward();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "World Pos: (%.2f, %.2f, %.2f)", wPos.x, wPos.y, wPos.z);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "World Fwd: (%.2f, %.2f, %.2f)", wFwd.x, wFwd.y, wFwd.z);
    }

    // Sockets Section
    if (ImGui::CollapsingHeader("Sockets & Attachments")) {
        auto& sockets = node->GetSockets();
        ImGui::Text("Sockets Count: %d", (int)sockets.size());
        for (int i = 0; i < (int)sockets.size(); ++i) {
            ImGui::PushID(i + 5000);
            ImGui::Text("Socket [%s]:", sockets[i].name.c_str());
            ImGui::DragFloat3("Rel Pos", &sockets[i].relativePosition.x, 0.05f);
            ImGui::DragFloat3("Rel Rot", &sockets[i].relativeRotation.x, 0.5f);
            ImGui::PopID();
        }

        static char s_socketNameBuf[64] = "Weapon_Socket";
        ImGui::InputText("New Socket Name", s_socketNameBuf, sizeof(s_socketNameBuf));
        if (ImGui::Button(" Add Socket")) {
            node->AddSocket(s_socketNameBuf);
        }
    }

    // Components Section
    if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& comp : node->GetComponents()) {
            if (comp) {
                ImGui::Separator();
                comp->DrawUI();
            }
        }

        ImGui::Spacing();
        if (ImGui::Button(" Add PointLight Component")) {
            if (renderer) {
                EngineRender::PointLight pl;
                pl.name = node->GetName() + "_PointLight";
                renderer->AddPointLight(pl);
                node->AddComponent<PointLightComponent>((int)renderer->GetPointLights().size() - 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(" Add SpotLight Component")) {
            if (renderer) {
                EngineRender::SpotLight sl;
                sl.name = node->GetName() + "_SpotLight";
                renderer->AddSpotLight(sl);
                node->AddComponent<SpotLightComponent>((int)renderer->GetSpotLights().size() - 1);
            }
        }
    }

    ImGui::Separator();
    if (node->GetId() != 0) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button(" Delete This Node", ImVec2(-1, 26))) {
            RemoveNode(node->GetId());
        }
        ImGui::PopStyleColor();
    }
#else
    (void)renderer;
    (void)camera;
#endif
}

} // namespace EngineScene
