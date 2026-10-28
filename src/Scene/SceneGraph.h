#pragma once

#include "SceneNode.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

class Renderer;
class Camera;

namespace EngineScene {

class SceneGraph {
public:
    static SceneGraph& Get();

    void Initialize();
    void Shutdown();

    std::shared_ptr<SceneNode> CreateNode(const std::string& name, std::shared_ptr<SceneNode> parent = nullptr);
    bool RemoveNode(uint32_t nodeId);
    void Clear();

    std::shared_ptr<SceneNode> FindNode(const std::string& name) const;
    std::shared_ptr<SceneNode> FindNodeById(uint32_t nodeId) const;
    std::shared_ptr<SceneNode> GetRoot() const { return m_root; }

    void Update(float dt);
    void SyncWithRenderer(Renderer* renderer,bool force=false);
    void SyncNodeFromModel(Renderer* renderer,uint32_t modelId);

    // Creates an interactive demo with parent, children, sockets, and moving lights
    void CreateDemoHierarchy(Renderer* renderer);

    // Editor UI
    void DrawUI(Renderer* renderer = nullptr, Camera* camera = nullptr);
    void ToggleUI() { m_uiOpen = !m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    bool IsUIOpen() const { return m_uiOpen; }

private:
    SceneGraph();
    ~SceneGraph() = default;

    std::shared_ptr<SceneNode> m_root;
    std::unordered_map<uint32_t, std::shared_ptr<SceneNode>> m_allNodes;
    std::shared_ptr<SceneNode> m_selectedNode;

    bool m_uiOpen = true;
    bool m_demoRotating = true;
    float m_demoRotationAngle = 0.0f;

    void DrawNodeHierarchyTree(const std::shared_ptr<SceneNode>& node);
    void DrawNodeInspector(Renderer* renderer, Camera* camera);
};

} // namespace EngineScene
