#pragma once

#include "Transform.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <iostream>

class Renderer;
class Camera;

namespace EngineScene {

class SceneNode;

// Base Component
class Component {
public:
    virtual ~Component() = default;
    virtual void OnAttach(SceneNode* owner) { m_owner = owner; }
    virtual void OnDetach() { m_owner = nullptr; }
    virtual void Update(float dt) {}
    virtual void DrawUI() {}
    virtual const char* GetTypeName() const = 0;

    SceneNode* GetOwner() const { return m_owner; }

protected:
    SceneNode* m_owner = nullptr;
};

// Named Socket on a SceneNode
struct Socket {
    std::string name;
    XMFLOAT3 relativePosition = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 relativeRotation = { 0.0f, 0.0f, 0.0f }; // Pitch, Yaw, Roll degrees

    XMMATRIX ComputeSocketMatrix() const {
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(relativeRotation.x),
            XMConvertToRadians(relativeRotation.y),
            XMConvertToRadians(relativeRotation.z)
        );
        XMMATRIX T = XMMatrixTranslation(relativePosition.x, relativePosition.y, relativePosition.z);
        return R * T;
    }
};

// Mesh Component - Binds a 3D Model from Renderer to this node's transform
class MeshComponent : public Component {
public:
    uint32_t modelId = 0;
    bool isDynamic = false;
    std::string modelName = "";

    MeshComponent(uint32_t id = 0, bool dynamic = false, const std::string& name = "")
        : modelId(id), isDynamic(dynamic), modelName(name) {}

    const char* GetTypeName() const override { return "MeshComponent"; }
    void Update(float dt) override;
    void DrawUI() override;
};

// Point Light Component - Attaches a point light to this node
class PointLightComponent : public Component {
public:
    int lightIndex = -1; // Index in Renderer::GetPointLights()
    XMFLOAT3 relativeOffset = { 0.0f, 0.0f, 0.0f };

    PointLightComponent(int idx = -1) : lightIndex(idx) {}

    const char* GetTypeName() const override { return "PointLightComponent"; }
    void Update(float dt) override;
    void DrawUI() override;
};

// Spot Light Component - Attaches a directional spot light to this node (e.g. Flashlight)
class SpotLightComponent : public Component {
public:
    int lightIndex = -1; // Index in Renderer::GetSpotLights()
    XMFLOAT3 relativeOffset = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 relativeDirection = { 0.0f, 0.0f, 1.0f };

    SpotLightComponent(int idx = -1) : lightIndex(idx) {}

    const char* GetTypeName() const override { return "SpotLightComponent"; }
    void Update(float dt) override;
    void DrawUI() override;
};

// Audio Emitter Component - Attaches a 3D spatial sound source to this node
class AudioEmitterComponent : public Component {
public:
    std::string soundPath = "";
    uint32_t soundId = 0;     // Runtime sound ID returned by AudioSystem::Play3D
    float volume = 1.0f;
    float maxDistance = 25.0f;
    bool isPlaying = false;
    bool loop = false;

    AudioEmitterComponent() = default;

    const char* GetTypeName() const override { return "AudioEmitterComponent"; }
    void Update(float dt) override;
    void DrawUI() override;
};

// Scene Graph Node (Hierarchical Transform & Component Container)
class SceneNode : public std::enable_shared_from_this<SceneNode> {
public:
    SceneNode(const std::string& name = "SceneNode", uint32_t id = UINT32_MAX);
    ~SceneNode();

    uint32_t GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    const std::string& GetTag() const { return m_tag; }
    void SetTag(const std::string& tag) { m_tag = tag; }

    bool IsActive() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    Transform& GetTransform() { return m_transform; }
    const Transform& GetTransform() const { return m_transform; }

    // Parent-Child hierarchy
    SceneNode* GetParent() const { return m_parent; }
    void SetParent(SceneNode* newParent, bool keepWorldTransform = true);
    void AddChild(std::shared_ptr<SceneNode> child);
    void RemoveChild(uint32_t childId);
    const std::vector<std::shared_ptr<SceneNode>>& GetChildren() const { return m_children; }

    // Sockets
    void AddSocket(const std::string& socketName, const XMFLOAT3& relPos = { 0, 0, 0 }, const XMFLOAT3& relRot = { 0, 0, 0 });
    bool GetSocketMatrix(const std::string& socketName, XMMATRIX& outMatrix) const;
    const std::vector<Socket>& GetSockets() const { return m_sockets; }
    std::vector<Socket>& GetSockets() { return m_sockets; }

    // Sockets attachment target
    void AttachToSocket(SceneNode* parentNode, const std::string& socketName);
    const std::string& GetAttachedSocketName() const { return m_attachedSocketName; }

    // Components
    template<typename T, typename... Args>
    std::shared_ptr<T> AddComponent(Args&&... args) {
        auto comp = std::make_shared<T>(std::forward<Args>(args)...);
        comp->OnAttach(this);
        m_components.push_back(comp);
        return comp;
    }

    template<typename T>
    std::shared_ptr<T> GetComponent() const {
        for (const auto& c : m_components) {
            auto casted = std::dynamic_pointer_cast<T>(c);
            if (casted) return casted;
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    template<typename T>
    void RemoveComponent() {
        for (auto it = m_components.begin(); it != m_components.end(); ++it) {
            if (std::dynamic_pointer_cast<T>(*it)) {
                (*it)->OnDetach();
                m_components.erase(it);
                return;
            }
        }
    }

    const std::vector<std::shared_ptr<Component>>& GetComponents() const { return m_components; }

    // Hierarchical update traversal
    void UpdateHierarchy(const XMMATRIX& parentWorldMatrix, float dt);

    static uint32_t GenerateId();

private:
    uint32_t m_id = 0;
    std::string m_name;
    std::string m_tag = "Untagged";
    bool m_active = true;

    Transform m_transform;

    SceneNode* m_parent = nullptr;
    std::string m_attachedSocketName = "";
    std::vector<std::shared_ptr<SceneNode>> m_children;
    std::vector<std::shared_ptr<Component>> m_components;
    std::vector<Socket> m_sockets;
};

} // namespace EngineScene
