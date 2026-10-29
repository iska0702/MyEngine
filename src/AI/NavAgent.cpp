#include "NavAgent.h"
#include "NavMesh.h"
#include <algorithm>
#include <cmath>

namespace EngineAI {

NavAgent::NavAgent(uint32_t id, const std::string& name)
    : m_id(id), m_name(name)
{
}

NavAgent::~NavAgent() = default;

bool NavAgent::SetDestination(const XMFLOAT3& destination, NavMesh& navMesh) {
    std::vector<XMFLOAT3> newPath;
    if (navMesh.FindPath(m_position, destination, newPath)) {
        m_targetDestination = destination;
        m_currentPath = std::move(newPath);
        m_waypointIndex = 0;
        return true;
    }
    Stop();
    return false;
}

void NavAgent::Stop() {
    m_currentPath.clear();
    m_waypointIndex = 0;
    m_velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
}

void NavAgent::AddPatrolWaypoint(const XMFLOAT3& pt) {
    m_patrolWaypoints.push_back(pt);
}

void NavAgent::ClearPatrolWaypoints() {
    m_patrolWaypoints.clear();
    m_currentPatrolIdx = 0;
}

XMFLOAT3 NavAgent::GetDebugColor() const {
    switch (m_state) {
        case AgentState::Idle:   return XMFLOAT3(0.6f, 0.6f, 0.6f);
        case AgentState::Patrol: return XMFLOAT3(0.2f, 0.8f, 1.0f); // Cyan
        case AgentState::Chase:  return XMFLOAT3(1.0f, 0.2f, 0.2f); // Red
        case AgentState::Wander: return XMFLOAT3(0.4f, 1.0f, 0.4f); // Green
        case AgentState::Flee:   return XMFLOAT3(1.0f, 0.8f, 0.1f); // Yellow
        default:                 return XMFLOAT3(1.0f, 1.0f, 1.0f);
    }
}

void NavAgent::Update(float dt, NavMesh& navMesh, const XMFLOAT3& playerPos) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    UpdateStateTransitions(dt, navMesh, playerPos);
    UpdateMovement(dt);
}

void NavAgent::UpdateStateTransitions(float dt, NavMesh& navMesh, const XMFLOAT3& playerPos) {
    float dx = playerPos.x - m_position.x;
    float dz = playerPos.z - m_position.z;
    float distToPlayer = std::sqrt(dx * dx + dz * dz);

    if (m_state == AgentState::Chase) {
        m_repathTimer += dt;
        if (distToPlayer > m_loseRange) {
            // Lost target
            m_state = m_patrolWaypoints.empty() ? AgentState::Wander : AgentState::Patrol;
            Stop();
        } else if (m_repathTimer >= 0.35f) {
            // Repath towards player
            m_repathTimer = 0.0f;
            SetDestination(playerPos, navMesh);
        }
    } else if (m_state == AgentState::Patrol) {
        if (distToPlayer <= m_detectionRange) {
            // Spotted player!
            m_state = AgentState::Chase;
            m_repathTimer = 0.0f;
            SetDestination(playerPos, navMesh);
        } else {
            // Patrol between waypoints
            bool atEndOfPath = m_currentPath.empty() || (m_waypointIndex >= m_currentPath.size());
            if (atEndOfPath && !m_patrolWaypoints.empty()) {
                m_patrolWaitTimer += dt;
                if (m_patrolWaitTimer >= 1.2f) {
                    m_patrolWaitTimer = 0.0f;
                    m_currentPatrolIdx = (m_currentPatrolIdx + 1) % m_patrolWaypoints.size();
                    SetDestination(m_patrolWaypoints[m_currentPatrolIdx], navMesh);
                }
            }
        }
    } else if (m_state == AgentState::Wander) {
        if (distToPlayer <= m_detectionRange) {
            m_state = AgentState::Chase;
            m_repathTimer = 0.0f;
            SetDestination(playerPos, navMesh);
        } else {
            bool atEndOfPath = m_currentPath.empty() || (m_waypointIndex >= m_currentPath.size());
            if (atEndOfPath) {
                m_wanderTimer += dt;
                if (m_wanderTimer >= 2.0f) {
                    m_wanderTimer = 0.0f;
                    XMFLOAT3 randomPt;
                    if (navMesh.GetRandomWalkablePoint(randomPt)) {
                        SetDestination(randomPt, navMesh);
                    }
                }
            }
        }
    } else if (m_state == AgentState::Flee) {
        // Run in opposite direction
        float oppX = m_position.x - dx;
        float oppZ = m_position.z - dz;
        XMFLOAT3 fleeGoal(oppX, m_position.y, oppZ);
        m_repathTimer += dt;
        if (m_repathTimer >= 0.5f) {
            m_repathTimer = 0.0f;
            SetDestination(fleeGoal, navMesh);
        }
    }
}

void NavAgent::UpdateMovement(float dt) {
    if (m_currentPath.empty() || m_waypointIndex >= m_currentPath.size()) {
        m_velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
        return;
    }

    float diffX = 0, diffZ = 0, dist = 0;
    while (m_waypointIndex < m_currentPath.size()) {
        const auto& target = m_currentPath[m_waypointIndex];
        diffX = target.x - m_position.x; diffZ = target.z - m_position.z;
        dist = std::sqrt(diffX * diffX + diffZ * diffZ);
        if (dist >= m_arrivalDistance) break;
        m_waypointIndex++;
    }
    if (m_waypointIndex >= m_currentPath.size()) { m_velocity = XMFLOAT3(0,0,0); return; }
    const auto& nextTarget = m_currentPath[m_waypointIndex];

    // Steering towards current waypoint
    float dirX = diffX / (std::max)(dist, 0.0001f);
    float dirZ = diffZ / (std::max)(dist, 0.0001f);

    float targetYaw = std::atan2(dirX, dirZ);

    // Smooth angle interpolation
    float yawDiff = targetYaw - m_yaw;
    while (yawDiff > 3.14159265f)  yawDiff -= 6.2831853f;
    while (yawDiff < -3.14159265f) yawDiff += 6.2831853f;
    m_yaw += std::clamp(yawDiff, -m_turnSpeed * dt, m_turnSpeed * dt);

    m_velocity.x = std::sin(m_yaw) * m_moveSpeed;
    m_velocity.y = 0.0f;
    m_velocity.z = std::cos(m_yaw) * m_moveSpeed;

    const float step = (std::min)(m_moveSpeed * dt, dist);
    m_position.x += std::sin(m_yaw) * step;
    m_position.z += std::cos(m_yaw) * step;
    m_position.y = nextTarget.y; // Match ground height
}

} // namespace EngineAI
