#pragma once

#include "NavMesh.h"
#include "NavAgent.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <DirectXMath.h>

class Renderer;
class Camera;

namespace EngineAI {

class NavigationSystem {
public:
    static NavigationSystem& Get();

    bool Initialize();
    void Shutdown();
    void Update(float dt, const XMFLOAT3& playerPosition);

    // NavMesh Operations
    bool BakeFromScene(const std::vector<Camera::CollisionBox>& obstacles,
                       float floorY = 0.0f,
                       const NavMeshParams* customParams = nullptr);

    bool FindPath(const XMFLOAT3& start, const XMFLOAT3& goal, std::vector<XMFLOAT3>& outPath);
    bool IsWalkable(const XMFLOAT3& pos) const;
    bool GetRandomWalkablePoint(XMFLOAT3& outPos) const;

    NavMesh& GetNavMesh() { return m_navMesh; }
    const NavMesh& GetNavMesh() const { return m_navMesh; }

    // AI Agents Management
    NavAgent* SpawnAgent(const XMFLOAT3& position,
                         AgentState initialState = AgentState::Patrol,
                         const std::string& name = "");
    void RemoveAgent(uint32_t agentId);
    void ClearAgents();
    NavAgent* GetAgent(uint32_t agentId);
    const std::vector<std::unique_ptr<NavAgent>>& GetAgents() const { return m_agents; }

    // Spawns a demo squad of patrolling and chasing AI drones
    void SpawnDemoSquad(const XMFLOAT3& centerPos);

    // Sync AI agent positions to dynamic models or visual representations
    void SyncWithRenderer(Renderer* renderer);

    // ImGui Editor UI
    void DrawUI(Renderer* renderer, const XMFLOAT3& playerPos, const std::vector<Camera::CollisionBox>& sceneObstacles);
    bool IsUIOpen() const { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI() { m_uiOpen = !m_uiOpen; }

private:
    NavigationSystem();
    ~NavigationSystem();
    NavigationSystem(const NavigationSystem&) = delete;
    NavigationSystem& operator=(const NavigationSystem&) = delete;

    NavMesh m_navMesh;
    std::vector<std::unique_ptr<NavAgent>> m_agents;
    uint32_t m_nextAgentId = 1;

    bool m_initialized = false;
    bool m_uiOpen = false;
    NavMeshParams m_currentParams;
    float m_defaultFloorY = 0.0f;

    // Track which model IDs were spawned for agents
    std::vector<uint32_t> m_agentModelIds;
};

} // namespace EngineAI
