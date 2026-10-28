#include "SceneNode.h"
#include "Renderer/Renderer.h"
#include "Audio/AudioSystem.h"
#include <algorithm>
#include <atomic>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineScene {

static std::atomic<uint32_t> s_nodeIdCounter{ 1 };

uint32_t SceneNode::GenerateId() {
    return s_nodeIdCounter++;
}

SceneNode::SceneNode(const std::string& name, uint32_t id)
    : m_name(name)
    , m_id(id == UINT32_MAX ? GenerateId() : id)
{
}

SceneNode::~SceneNode() {
    for (auto& comp : m_components) {
        if (comp) comp->OnDetach();
    }
    m_components.clear();
    m_children.clear();
}

void SceneNode::SetParent(SceneNode* newParent, bool keepWorldTransform) {
    if (m_parent == newParent) return;
    if (m_id == 0) return;
    for (auto* ancestor = newParent; ancestor; ancestor = ancestor->m_parent) {
        if (ancestor == this) return;
    }
    // Retain ownership until all member accesses after detaching are complete.
    auto self = shared_from_this();

    XMFLOAT3 savedWorldPos = m_transform.GetWorldPosition();

    if (m_parent) {
        // Remove from previous parent's child list
        auto& sibs = m_parent->m_children;
        for (auto it = sibs.begin(); it != sibs.end(); ++it) {
            if (it->get() == this) {
                // Keep shared_ptr alive during re-parenting
                std::shared_ptr<SceneNode> self = *it;
                sibs.erase(it);
                if (newParent) {
                    newParent->m_children.push_back(self);
                }
                break;
            }
        }
    } else if (newParent) {
        // Was orphan/root child
        newParent->m_children.push_back(shared_from_this());
    }

    m_parent = newParent;
    m_attachedSocketName.clear();

    if (keepWorldTransform && m_parent) {
        // Invert parent world matrix to recalculate local position
        XMMATRIX parentWorld = XMLoadFloat4x4(&m_parent->GetTransform().worldMatrix);
        XMVECTOR det;
        XMMATRIX invParent = XMMatrixInverse(&det, parentWorld);
        XMVECTOR worldPosVec = XMLoadFloat3(&savedWorldPos);
        XMVECTOR newLocalPos = XMVector3Transform(worldPosVec, invParent);
        XMStoreFloat3(&m_transform.localPosition, newLocalPos);
    }

    m_transform.isDirty = true;
}

void SceneNode::AddChild(std::shared_ptr<SceneNode> child) {
    if (!child || child.get() == this) return;
    child->SetParent(this);
}

void SceneNode::RemoveChild(uint32_t childId) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if ((*it)->GetId() == childId) {
            (*it)->m_parent = nullptr;
            m_children.erase(it);
            return;
        }
    }
}

void SceneNode::AddSocket(const std::string& socketName, const XMFLOAT3& relPos, const XMFLOAT3& relRot) {
    for (auto& s : m_sockets) {
        if (s.name == socketName) {
            s.relativePosition = relPos;
            s.relativeRotation = relRot;
            return;
        }
    }
    m_sockets.push_back({ socketName, relPos, relRot });
}

bool SceneNode::GetSocketMatrix(const std::string& socketName, XMMATRIX& outMatrix) const {
    for (const auto& s : m_sockets) {
        if (s.name == socketName) {
            XMMATRIX socketLocal = s.ComputeSocketMatrix();
            XMMATRIX nodeWorld = XMLoadFloat4x4(&m_transform.worldMatrix);
            outMatrix = socketLocal * nodeWorld;
            return true;
        }
    }
    return false;
}

void SceneNode::AttachToSocket(SceneNode* parentNode, const std::string& socketName) {
    SetParent(parentNode, false);
    m_attachedSocketName = socketName;
    m_transform.localPosition = { 0.0f, 0.0f, 0.0f };
    m_transform.localRotation = { 0.0f, 0.0f, 0.0f };
    m_transform.isDirty = true;
}

void SceneNode::UpdateHierarchy(const XMMATRIX& parentWorldMatrix, float dt) {
    if (!m_active) return;

    XMMATRIX effectiveParent = parentWorldMatrix;
    if (m_parent && !m_attachedSocketName.empty()) {
        XMMATRIX socketMatrix;
        if (m_parent->GetSocketMatrix(m_attachedSocketName, socketMatrix)) {
            effectiveParent = socketMatrix;
        }
    }

    m_transform.Update(effectiveParent);

    // Update all components attached to this node
    for (auto& comp : m_components) {
        if (comp) comp->Update(dt);
    }

    // Recursively propagate world matrix down the branch
    XMMATRIX nodeWorld = XMLoadFloat4x4(&m_transform.worldMatrix);
    for (auto& child : m_children) {
        if (child) child->UpdateHierarchy(nodeWorld, dt);
    }
}

// -----------------------------------------------------------------------------
// Component Implementations
// -----------------------------------------------------------------------------

void MeshComponent::Update(float dt) {
    if (!m_owner) return;

    // Synchronize this node's world transform to the renderer's model
    // Note: Renderer models get rendered with this worldMatrix
    XMFLOAT3 worldPos = m_owner->GetTransform().GetWorldPosition();
    const auto& wMat = m_owner->GetTransform().worldMatrix;

    // For static models in Renderer
    // Sync will be processed by SceneGraph::SyncWithRenderer
}

void MeshComponent::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    ImGui::Text("Mesh Component: %s (ID: %u, Dynamic: %s)",
                modelName.c_str(), modelId, isDynamic ? "Yes" : "No");
#endif
}

void PointLightComponent::Update(float dt) {
    if (!m_owner) return;
    // World position of the light follows the node
    XMFLOAT3 pos = m_owner->GetTransform().GetWorldPosition();
    pos.x += relativeOffset.x;
    pos.y += relativeOffset.y;
    pos.z += relativeOffset.z;

    // Synced with Renderer in SceneGraph::SyncWithRenderer
}

void PointLightComponent::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    ImGui::Text("Point Light Component (Light Index: %d)", lightIndex);
    ImGui::DragFloat3("Offset", &relativeOffset.x, 0.05f);
#endif
}

void SpotLightComponent::Update(float dt) {
    if (!m_owner) return;
    // Light follows node world position and forward direction
}

void SpotLightComponent::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    ImGui::Text("Spot Light Component (Light Index: %d)", lightIndex);
    ImGui::DragFloat3("Offset", &relativeOffset.x, 0.05f);
    ImGui::DragFloat3("Rel Dir", &relativeDirection.x, 0.05f);
#endif
}

void AudioEmitterComponent::Update(float dt) {
    if (!m_owner || !isPlaying) return;
    XMFLOAT3 pos = m_owner->GetTransform().GetWorldPosition();
    if (soundId != 0) {
        EngineAudio::AudioSystem::Get().SetSoundPosition(soundId, pos.x, pos.y, pos.z);
    }
}

void AudioEmitterComponent::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    ImGui::Text("Audio Emitter: %s", soundPath.c_str());
    ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f);
    ImGui::SliderFloat("Max Dist", &maxDistance, 1.0f, 100.0f);
    ImGui::Checkbox("Playing", &isPlaying);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loop);
#endif
}

} // namespace EngineScene
