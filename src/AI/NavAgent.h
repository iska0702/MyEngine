#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>

namespace EngineAI {

using namespace DirectX;
class NavMesh;

enum class AgentState {
    Idle = 0,
    Patrol,
    Chase,
    Wander,
    Flee
};

class NavAgent {
public:
    NavAgent(uint32_t id, const std::string& name = "AI_Agent");
    ~NavAgent();

    void Update(float dt, NavMesh& navMesh, const XMFLOAT3& playerPos);

    // Navigation Commands
    bool SetDestination(const XMFLOAT3& destination, NavMesh& navMesh);
    void Stop();

    // Patrol route setup
    void AddPatrolWaypoint(const XMFLOAT3& pt);
    void ClearPatrolWaypoints();

    // Getters & Setters
    uint32_t GetId() const { return m_id; }
    uint32_t GetModelId() const { return m_modelId; }
    void SetModelId(uint32_t id) { m_modelId = id; }
    const XMFLOAT3& GetDestination() const { return m_targetDestination; }
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    AgentState GetState() const { return m_state; }
    void SetState(AgentState state) { m_state = state; }

    const XMFLOAT3& GetPosition() const { return m_position; }
    void SetPosition(const XMFLOAT3& pos) { m_position = pos; }

    const XMFLOAT3& GetVelocity() const { return m_velocity; }
    float GetYaw() const { return m_yaw; }

    float GetSpeed() const { return m_moveSpeed; }
    void SetSpeed(float speed) { m_moveSpeed = speed; }

    float GetDetectionRange() const { return m_detectionRange; }
    void SetDetectionRange(float range) { m_detectionRange = range; }

    const std::vector<XMFLOAT3>& GetCurrentPath() const { return m_currentPath; }
    size_t GetWaypointIndex() const { return m_waypointIndex; }
    const std::vector<XMFLOAT3>& GetPatrolWaypoints() const { return m_patrolWaypoints; }

    XMFLOAT3 GetDebugColor() const;

private:
    void UpdateMovement(float dt);
    void UpdateStateTransitions(float dt, NavMesh& navMesh, const XMFLOAT3& playerPos);

    uint32_t m_id = 0;
    uint32_t m_modelId = 0;
    std::string m_name;
    AgentState m_state = AgentState::Patrol;

    XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 m_velocity = { 0.0f, 0.0f, 0.0f };
    float m_yaw = 0.0f; // in radians

    XMFLOAT3 m_targetDestination = { 0.0f, 0.0f, 0.0f };
    std::vector<XMFLOAT3> m_currentPath;
    size_t m_waypointIndex = 0;

    std::vector<XMFLOAT3> m_patrolWaypoints;
    size_t m_currentPatrolIdx = 0;

    float m_moveSpeed = 3.5f;
    float m_turnSpeed = 8.0f;
    float m_arrivalDistance = 0.4f;
    float m_detectionRange = 18.0f;
    float m_loseRange = 25.0f;
    float m_repathTimer = 0.0f;
    float m_wanderTimer = 0.0f;
    float m_patrolWaitTimer = 0.0f;
};

} // namespace EngineAI
