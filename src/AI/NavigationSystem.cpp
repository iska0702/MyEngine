#include "NavigationSystem.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"
#include <algorithm>
#include <iostream>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineAI {

NavigationSystem& NavigationSystem::Get() {
    static NavigationSystem instance;
    return instance;
}

NavigationSystem::NavigationSystem() = default;
NavigationSystem::~NavigationSystem() {
    Shutdown();
}

bool NavigationSystem::Initialize() {
    if (m_initialized) return true;
    m_initialized = true;

    // Default bake with empty obstacles so queries succeed before first bake
    std::vector<Camera::CollisionBox> emptyObstacles;
    m_navMesh.Bake(m_currentParams, emptyObstacles, m_defaultFloorY);

    // Spawn demo patrol squad around center
    SpawnDemoSquad(XMFLOAT3(0.0f, 0.0f, 0.0f));

    return true;
}

void NavigationSystem::Shutdown() {
    ClearAgents();
    m_initialized = false;
}

bool NavigationSystem::BakeFromScene(const std::vector<Camera::CollisionBox>& obstacles,
                                    float floorY,
                                    const NavMeshParams* customParams) {
    if (customParams) {
        m_currentParams = *customParams;
    }
    m_defaultFloorY = floorY;

    bool success = m_navMesh.Bake(m_currentParams, obstacles, floorY);

    // After re-baking, restart path for all agents towards their current destinations
    for (auto& agent : m_agents) {
        if (agent) {
            agent->SetDestination(agent->GetPosition(), m_navMesh);
        }
    }
    return success;
}

void NavigationSystem::Update(float dt, const XMFLOAT3& playerPosition) {
    if (!m_initialized) return;

    for (auto& agent : m_agents) {
        if (agent) {
            agent->Update(dt, m_navMesh, playerPosition);
        }
    }
}

bool NavigationSystem::FindPath(const XMFLOAT3& start, const XMFLOAT3& goal, std::vector<XMFLOAT3>& outPath) {
    return m_navMesh.FindPath(start, goal, outPath);
}

bool NavigationSystem::IsWalkable(const XMFLOAT3& pos) const {
    return m_navMesh.IsWalkable(pos);
}

bool NavigationSystem::GetRandomWalkablePoint(XMFLOAT3& outPos) const {
    return m_navMesh.GetRandomWalkablePoint(outPos);
}

NavAgent* NavigationSystem::SpawnAgent(const XMFLOAT3& position,
                                      AgentState initialState,
                                      const std::string& name) {
    uint32_t id = m_nextAgentId++;
    std::string agentName = name.empty() ? ("AI_Agent_" + std::to_string(id)) : name;

    auto agent = std::make_unique<NavAgent>(id, agentName);
    agent->SetPosition(position);
    agent->SetState(initialState);

    NavAgent* ptr = agent.get();
    m_agents.push_back(std::move(agent));
    return ptr;
}

void NavigationSystem::RemoveAgent(uint32_t agentId) {
    auto it = std::remove_if(m_agents.begin(), m_agents.end(),
                             [agentId](const std::unique_ptr<NavAgent>& a) {
                                 return a && a->GetId() == agentId;
                             });
    m_agents.erase(it, m_agents.end());
}

void NavigationSystem::ClearAgents() {
    m_agents.clear();
}

NavAgent* NavigationSystem::GetAgent(uint32_t agentId) {
    for (auto& a : m_agents) {
        if (a && a->GetId() == agentId) return a.get();
    }
    return nullptr;
}

void NavigationSystem::SpawnDemoSquad(const XMFLOAT3& centerPos) {
    ClearAgents();

    // 1. Patrol Drone Alpha - squares around perimeter
    NavAgent* droneA = SpawnAgent(XMFLOAT3(centerPos.x - 8.0f, centerPos.y, centerPos.z - 8.0f),
                                  AgentState::Patrol, "Drone Patrol Alpha");
    if (droneA) {
        droneA->SetSpeed(3.8f);
        droneA->AddPatrolWaypoint(XMFLOAT3(centerPos.x - 8.0f, centerPos.y, centerPos.z - 8.0f));
        droneA->AddPatrolWaypoint(XMFLOAT3(centerPos.x + 8.0f, centerPos.y, centerPos.z - 8.0f));
        droneA->AddPatrolWaypoint(XMFLOAT3(centerPos.x + 8.0f, centerPos.y, centerPos.z + 8.0f));
        droneA->AddPatrolWaypoint(XMFLOAT3(centerPos.x - 8.0f, centerPos.y, centerPos.z + 8.0f));
        droneA->SetDestination(XMFLOAT3(centerPos.x + 8.0f, centerPos.y, centerPos.z - 8.0f), m_navMesh);
    }

    // 2. Hunter Mech Beta - chases player aggressively
    NavAgent* hunter = SpawnAgent(XMFLOAT3(centerPos.x + 12.0f, centerPos.y, centerPos.z + 4.0f),
                                  AgentState::Chase, "Hunter Mech Beta");
    if (hunter) {
        hunter->SetSpeed(4.5f);
        hunter->SetDetectionRange(25.0f);
    }

    // 3. Scout Recon Gamma - wanderer exploring the map
    NavAgent* scout = SpawnAgent(XMFLOAT3(centerPos.x - 6.0f, centerPos.y, centerPos.z + 10.0f),
                                 AgentState::Wander, "Scout Recon Gamma");
    if (scout) {
        scout->SetSpeed(3.0f);
    }
}

void NavigationSystem::SyncWithRenderer(Renderer* renderer) {
    if (!renderer) return;

    // Synchronize agent positions and yaw headings to dynamic models if available
    auto& dynModels = renderer->GetDynamicModels();
    for (size_t i = 0; i < m_agents.size() && i < dynModels.size(); ++i) {
        if (m_agents[i]) {
            const auto& pos = m_agents[i]->GetPosition();
            dynModels[i].position = pos;
            dynModels[i].rotation.y = XMConvertToDegrees(m_agents[i]->GetYaw());
        }
    }
}

void NavigationSystem::DrawUI(Renderer* renderer, const XMFLOAT3& playerPos,
                              const std::vector<Camera::CollisionBox>& sceneObstacles) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Navigation & AI Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "NavMesh Status:");
    ImGui::Text("Dimensions: %dx%d (%zu cells)", m_navMesh.GetGridWidth(), m_navMesh.GetGridHeight(), m_navMesh.GetTotalCells());
    ImGui::Text("Walkable Cells: %zu / %zu (%.1f%%)",
                m_navMesh.GetWalkableCells(),
                m_navMesh.GetTotalCells(),
                m_navMesh.GetTotalCells() > 0 ? (100.0f * m_navMesh.GetWalkableCells() / m_navMesh.GetTotalCells()) : 0.0f);
    ImGui::Text("Cell Size: %.2fm | Obstacles: %zu", m_currentParams.cellSize, sceneObstacles.size());

    ImGui::Separator();

    if (ImGui::Button(" Bake NavMesh (From Scene)")) {
        BakeFromScene(sceneObstacles, m_defaultFloorY);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Spawn Demo Squad")) {
        SpawnDemoSquad(playerPos);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "AI Agents (%zu active):", m_agents.size());

    if (ImGui::Button(" Spawn Agent at Camera")) {
        SpawnAgent(playerPos, AgentState::Patrol);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Clear All Agents")) {
        ClearAgents();
    }

    ImGui::Separator();

    int agentToRemove = -1;
    for (int i = 0; i < static_cast<int>(m_agents.size()); ++i) {
        auto& agent = m_agents[i];
        if (!agent) continue;

        ImGui::PushID(i + 5000);
        char label[128];
        const char* stateNames[] = { "Idle", "Patrol", "Chase", "Wander", "Flee" };
        snprintf(label, sizeof(label), "[%d] %s (%s)##%d",
                 agent->GetId(), agent->GetName().c_str(), stateNames[static_cast<int>(agent->GetState())], i);

        XMFLOAT3 col = agent->GetDebugColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(col.x, col.y, col.z, 1.0f));
        bool opened = ImGui::TreeNode(label);
        ImGui::PopStyleColor();

        if (opened) {
            // State dropdown
            int curState = static_cast<int>(agent->GetState());
            if (ImGui::Combo("Behavior State", &curState, stateNames, IM_ARRAYSIZE(stateNames))) {
                agent->SetState(static_cast<AgentState>(curState));
            }

            // Speed slider
            float speed = agent->GetSpeed();
            if (ImGui::SliderFloat("Move Speed", &speed, 0.5f, 15.0f, "%.1f m/s")) {
                agent->SetSpeed(speed);
            }

            // Detection range
            float detRange = agent->GetDetectionRange();
            if (ImGui::SliderFloat("Detection Range", &detRange, 2.0f, 50.0f, "%.1f m")) {
                agent->SetDetectionRange(detRange);
            }

            // Telemetry
            const auto& pos = agent->GetPosition();
            ImGui::Text("Pos: (%.1f, %.1f, %.1f) | Yaw: %.1f deg",
                        pos.x, pos.y, pos.z, XMConvertToDegrees(agent->GetYaw()));

            const auto& path = agent->GetCurrentPath();
            ImGui::Text("Path Waypoints: %zu (Current Target: %zu)", path.size(), agent->GetWaypointIndex());

            if (ImGui::Button(" Order to Player Cam")) {
                agent->SetDestination(playerPos, m_navMesh);
            }
            ImGui::SameLine();
            if (ImGui::Button(" Stop")) {
                agent->Stop();
            }
            ImGui::SameLine();
            if (ImGui::Button(" Remove")) {
                agentToRemove = static_cast<int>(agent->GetId());
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (agentToRemove != -1) {
        RemoveAgent(static_cast<uint32_t>(agentToRemove));
    }

    ImGui::End();
#endif
}

} // namespace EngineAI
