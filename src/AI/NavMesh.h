#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <DirectXMath.h>
#include "Renderer/Camera.h"

namespace EngineAI {

using namespace DirectX;

// Cell flag in the navigation grid
enum NavCellFlags : uint8_t {
    NavCell_Blocked    = 0,
    NavCell_Walkable   = 1 << 0,
    NavCell_Hazard     = 1 << 1,
    NavCell_Jumpable   = 1 << 2
};

// Represents a node in the navigation mesh/grid
struct NavNode {
    int x = 0;
    int z = 0;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    uint8_t flags = NavCell_Walkable;
    float costMultiplier = 1.0f;
};

// Navigation Mesh parameters
struct NavMeshParams {
    XMFLOAT3 minBounds = { -50.0f, -2.0f, -50.0f };
    XMFLOAT3 maxBounds = {  50.0f, 15.0f,  50.0f };
    float cellSize     = 0.5f;   // 0.5 meter per cell
    float agentRadius  = 0.45f;  // Player/Agent radius for collision inflation
    float agentHeight  = 1.8f;   // Clearance height
    float maxClimb     = 0.4f;   // Max step height (stairs/curbs)
    float maxSlopeDeg  = 45.0f;  // Max slope angle
};

class NavMesh {
public:
    NavMesh();
    ~NavMesh();

    // Generation / Baking
    bool Bake(const NavMeshParams& params,
              const std::vector<Camera::CollisionBox>& obstacles,
              float defaultFloorY = 0.0f);

    // Queries
    bool FindPath(const XMFLOAT3& startPos, const XMFLOAT3& targetPos,
                  std::vector<XMFLOAT3>& outPath);

    bool HasLineOfSight(const XMFLOAT3& from, const XMFLOAT3& to) const;
    bool IsWalkable(const XMFLOAT3& worldPos) const;
    bool FindNearestWalkablePosition(const XMFLOAT3& worldPos, XMFLOAT3& outPos, float searchRadius = 3.0f) const;
    bool GetRandomWalkablePoint(XMFLOAT3& outPos) const;

    // Grid coordinates conversion
    bool WorldToGrid(const XMFLOAT3& worldPos, int& outX, int& outZ) const;
    XMFLOAT3 GridToWorld(int gx, int gz) const;

    // Statistics
    bool IsBaked() const { return m_baked; }
    size_t GetTotalCells() const { return m_nodes.size(); }
    size_t GetWalkableCells() const { return m_walkableCount; }
    int GetGridWidth() const { return m_width; }
    int GetGridHeight() const { return m_height; }
    const NavMeshParams& GetParams() const { return m_params; }

    // Direct access for debug visualization
    const std::vector<NavNode>& GetNodes() const { return m_nodes; }

private:
    void SmoothPath(std::vector<XMFLOAT3>& path) const;
    inline size_t GetNodeIndex(int x, int z) const { return static_cast<size_t>(z * m_width + x); }
    inline bool IsValidCoord(int x, int z) const { return x >= 0 && x < m_width && z >= 0 && z < m_height; }

    NavMeshParams m_params;
    int m_width  = 0;
    int m_height = 0;
    std::vector<NavNode> m_nodes;
    size_t m_walkableCount = 0;
    bool m_baked = false;
};

} // namespace EngineAI
