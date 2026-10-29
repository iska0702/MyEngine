#pragma once

#include "Prefab.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneGraph.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <DirectXMath.h>

class Renderer;

namespace EnginePrefab {

class PrefabSystem {
public:
    static PrefabSystem& Get();

    bool Initialize();
    void Shutdown();

    // Prefab Management
    void ScanPrefabs(const std::string& directory = "");
    std::shared_ptr<Prefab> LoadPrefab(const std::string& filepath);
    bool SavePrefab(const std::shared_ptr<Prefab>& prefab, const std::string& filepath = "");
    std::shared_ptr<Prefab> GetPrefab(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<Prefab>>& GetLoadedPrefabs() const { return m_prefabs; }

    // Archetype conversion from live scene
    std::shared_ptr<Prefab> CreatePrefabFromNode(
        EngineScene::SceneNode* node,
        const std::string& prefabName,
        Renderer* renderer = nullptr,
        const std::string& saveDirectory = ""
    );

    // Instantiation
    std::shared_ptr<EngineScene::SceneNode> Instantiate(
        const std::string& prefabName,
        std::shared_ptr<EngineScene::SceneNode> parent = nullptr,
        const DirectX::XMFLOAT3& worldPos = { 0.0f, 0.0f, 0.0f },
        const DirectX::XMFLOAT3& worldRot = { 0.0f, 0.0f, 0.0f },
        const DirectX::XMFLOAT3& worldScale = { 1.0f, 1.0f, 1.0f }
    );

    std::shared_ptr<EngineScene::SceneNode> InstantiateWithRenderer(
        const std::string& prefabName,
        Renderer* renderer,
        std::shared_ptr<EngineScene::SceneNode> parent = nullptr,
        const DirectX::XMFLOAT3& worldPos = { 0.0f, 0.0f, 0.0f },
        const DirectX::XMFLOAT3& worldRot = { 0.0f, 0.0f, 0.0f },
        const DirectX::XMFLOAT3& worldScale = { 1.0f, 1.0f, 1.0f }
    );

    // Starter Procedural Templates
    void CreateDefaultPrefabs();

    // UI
    void DrawUI(Renderer* renderer, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward);
    void ToggleUI() { m_uiOpen = !m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    bool IsUIOpen() const { return m_uiOpen; }

private:
    PrefabSystem() = default;
    ~PrefabSystem() = default;
    PrefabSystem(const PrefabSystem&) = delete;
    PrefabSystem& operator=(const PrefabSystem&) = delete;

    std::unordered_map<std::string, std::shared_ptr<Prefab>> m_prefabs;
    bool m_initialized = false;
    bool m_uiOpen = false;

    // UI state
    std::string m_selectedPrefabName = "";
    char m_newPrefabName[64] = "New_Archetype";
    uint32_t m_selectedSceneNodeId = 0;

    void ExtractNodeData(EngineScene::SceneNode* node, PrefabNodeData& outData, Renderer* renderer);
    std::shared_ptr<EngineScene::SceneNode> BuildNodeHierarchy(
        const PrefabNodeData& data,
        std::shared_ptr<EngineScene::SceneNode> parent,
        Renderer* renderer,
        const std::string& prefabName
    );
};

} // namespace EnginePrefab
