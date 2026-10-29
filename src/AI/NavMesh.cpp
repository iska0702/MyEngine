#include "NavMesh.h"
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

namespace EngineAI {

NavMesh::NavMesh() = default;
NavMesh::~NavMesh() = default;

bool NavMesh::Bake(const NavMeshParams& params,
                   const std::vector<Camera::CollisionBox>& obstacles,
                   float defaultFloorY) {
    m_params = params;
    float rangeX = params.maxBounds.x - params.minBounds.x;
    float rangeZ = params.maxBounds.z - params.minBounds.z;
    if (rangeX <= 0.0f || rangeZ <= 0.0f || params.cellSize <= 0.05f) {
        return false;
    }

    m_width  = static_cast<int>(std::ceil(rangeX / params.cellSize));
    m_height = static_cast<int>(std::ceil(rangeZ / params.cellSize));

    m_nodes.resize(static_cast<size_t>(m_width) * m_height);
    m_walkableCount = 0;

    for (int z = 0; z < m_height; ++z) {
        for (int x = 0; x < m_width; ++x) {
            size_t idx = GetNodeIndex(x, z);
            NavNode& node = m_nodes[idx];
            node.x = x;
            node.z = z;
            node.worldX = params.minBounds.x + (static_cast<float>(x) + 0.5f) * params.cellSize;
            node.worldY = defaultFloorY;
            node.worldZ = params.minBounds.z + (static_cast<float>(z) + 0.5f) * params.cellSize;
            node.flags = NavCell_Walkable;
            node.costMultiplier = 1.0f;
        }
    }

    // Rasterize obstacles into the grid (inflated by agentRadius)
    float inflation = params.agentRadius;
    for (const auto& box : obstacles) {
        float minX = box.minPoint.x - inflation;
        float maxX = box.maxPoint.x + inflation;
        float minY = box.minPoint.y;
        float maxY = box.maxPoint.y;
        float minZ = box.minPoint.z - inflation;
        float maxZ = box.maxPoint.z + inflation;

        // Check if obstacle height overlaps the walkable floor clearance
        if (maxY < defaultFloorY || minY > (defaultFloorY + params.agentHeight)) {
            continue;
        }

        int startX = (std::max)(0, static_cast<int>(std::floor((minX - params.minBounds.x) / params.cellSize)));
        int endX   = (std::min)(m_width - 1, static_cast<int>(std::floor((maxX - params.minBounds.x) / params.cellSize)));
        int startZ = (std::max)(0, static_cast<int>(std::floor((minZ - params.minBounds.z) / params.cellSize)));
        int endZ   = (std::min)(m_height - 1, static_cast<int>(std::floor((maxZ - params.minBounds.z) / params.cellSize)));

        for (int z = startZ; z <= endZ; ++z) {
            for (int x = startX; x <= endX; ++x) {
                size_t idx = GetNodeIndex(x, z);
                m_nodes[idx].flags = NavCell_Blocked;
            }
        }
    }

    // Count walkable cells
    for (const auto& node : m_nodes) {
        if (node.flags & NavCell_Walkable) {
            m_walkableCount++;
        }
    }

    m_baked = true;
    return true;
}

bool NavMesh::WorldToGrid(const XMFLOAT3& worldPos, int& outX, int& outZ) const {
    if (!m_baked || m_width <= 0 || m_height <= 0) return false;
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.z)) return false;
    float relX = worldPos.x - m_params.minBounds.x;
    float relZ = worldPos.z - m_params.minBounds.z;
    if (relX < 0 || relZ < 0 || relX >= m_width*m_params.cellSize || relZ >= m_height*m_params.cellSize) return false;
    outX = static_cast<int>(std::floor(relX / m_params.cellSize));
    outZ = static_cast<int>(std::floor(relZ / m_params.cellSize));
    return IsValidCoord(outX, outZ);
}

XMFLOAT3 NavMesh::GridToWorld(int gx, int gz) const {
    if (!IsValidCoord(gx, gz)) return XMFLOAT3(0.0f, 0.0f, 0.0f);
    size_t idx = GetNodeIndex(gx, gz);
    return XMFLOAT3(m_nodes[idx].worldX, m_nodes[idx].worldY, m_nodes[idx].worldZ);
}

bool NavMesh::IsWalkable(const XMFLOAT3& worldPos) const {
    int gx, gz;
    if (!WorldToGrid(worldPos, gx, gz)) return false;
    return (m_nodes[GetNodeIndex(gx, gz)].flags & NavCell_Walkable) != 0;
}

bool NavMesh::FindNearestWalkablePosition(const XMFLOAT3& worldPos, XMFLOAT3& outPos, float searchRadius) const {
    if (!m_baked || m_width<=0 || m_height<=0 || !std::isfinite(worldPos.x) || !std::isfinite(worldPos.z) || !std::isfinite(searchRadius) || searchRadius<0) return false;
    const XMFLOAT3 input=worldPos; // The caller may pass the same object for input and output.
    int cx, cz;
    if (!WorldToGrid(input, cx, cz)) {
        const float maxX=m_params.minBounds.x+m_width*m_params.cellSize;
        const float maxZ=m_params.minBounds.z+m_height*m_params.cellSize;
        if(input.x<m_params.minBounds.x-searchRadius || input.x>maxX+searchRadius || input.z<m_params.minBounds.z-searchRadius || input.z>maxZ+searchRadius) return false;
        cx = (std::clamp)(static_cast<int>(std::floor((input.x-m_params.minBounds.x)/m_params.cellSize)), 0, m_width - 1);
        cz = (std::clamp)(static_cast<int>(std::floor((input.z-m_params.minBounds.z)/m_params.cellSize)), 0, m_height - 1);
    }

    int rSteps = static_cast<int>(std::ceil(searchRadius / m_params.cellSize));
    float closestDistSq = 1e9f;
    bool found = false;

    for (int dz = -rSteps; dz <= rSteps; ++dz) {
        for (int dx = -rSteps; dx <= rSteps; ++dx) {
            int nx = cx + dx;
            int nz = cz + dz;
            if (!IsValidCoord(nx, nz)) continue;
            size_t idx = GetNodeIndex(nx, nz);
            if (m_nodes[idx].flags & NavCell_Walkable) {
                float xDiff=m_nodes[idx].worldX-input.x, zDiff=m_nodes[idx].worldZ-input.z;
                float distSq = xDiff*xDiff+zDiff*zDiff;
                if (distSq > searchRadius*searchRadius) continue;
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    outPos = XMFLOAT3(m_nodes[idx].worldX, m_nodes[idx].worldY, m_nodes[idx].worldZ);
                    found = true;
                }
            }
        }
    }
    return found;
}

bool NavMesh::GetRandomWalkablePoint(XMFLOAT3& outPos) const {
    if (!m_baked || m_walkableCount == 0) return false;
    // Attempt random sampling up to 30 times
    for (int attempt = 0; attempt < 30; ++attempt) {
        int rx = std::rand() % m_width;
        int rz = std::rand() % m_height;
        size_t idx = GetNodeIndex(rx, rz);
        if (m_nodes[idx].flags & NavCell_Walkable) {
            outPos = XMFLOAT3(m_nodes[idx].worldX, m_nodes[idx].worldY, m_nodes[idx].worldZ);
            return true;
        }
    }
    return false;
}

bool NavMesh::HasLineOfSight(const XMFLOAT3& from, const XMFLOAT3& to) const {
    int x0, z0, x1, z1;
    if (!WorldToGrid(from, x0, z0) || !WorldToGrid(to, x1, z1)) return false;

    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;

    int curX = x0;
    int curZ = z0;

    while (true) {
        if (!IsValidCoord(curX, curZ)) return false;
        if (!(m_nodes[GetNodeIndex(curX, curZ)].flags & NavCell_Walkable)) {
            return false;
        }
        if (curX == x1 && curZ == z1) break;

        int e2 = 2 * err;
        if (e2 > -dz) {
            err -= dz;
            curX += sx;
        }
        if (e2 < dx) {
            err += dx;
            curZ += sz;
        }
    }
    return true;
}

void NavMesh::SmoothPath(std::vector<XMFLOAT3>& path) const {
    if (path.size() <= 2) return;

    std::vector<XMFLOAT3> smoothed;
    smoothed.push_back(path.front());

    size_t currentIdx = 0;
    while (currentIdx < path.size() - 1) {
        size_t furthestVisible = currentIdx + 1;
        for (size_t nextIdx = path.size() - 1; nextIdx > currentIdx + 1; --nextIdx) {
            if (HasLineOfSight(path[currentIdx], path[nextIdx])) {
                furthestVisible = nextIdx;
                break;
            }
        }
        smoothed.push_back(path[furthestVisible]);
        currentIdx = furthestVisible;
    }

    path = std::move(smoothed);
}

bool NavMesh::FindPath(const XMFLOAT3& startPos, const XMFLOAT3& targetPos,
                       std::vector<XMFLOAT3>& outPath) {
    outPath.clear();
    if (!m_baked) return false;

    int sx, sz, gx, gz;
    XMFLOAT3 validStart = startPos;
    XMFLOAT3 validGoal  = targetPos;

    if (!WorldToGrid(validStart, sx, sz) || !(m_nodes[GetNodeIndex(sx, sz)].flags & NavCell_Walkable)) {
        if (!FindNearestWalkablePosition(validStart, validStart)) return false;
        WorldToGrid(validStart, sx, sz);
    }
    if (!WorldToGrid(validGoal, gx, gz) || !(m_nodes[GetNodeIndex(gx, gz)].flags & NavCell_Walkable)) {
        if (!FindNearestWalkablePosition(validGoal, validGoal)) return false;
        WorldToGrid(validGoal, gx, gz);
    }

    if (sx == gx && sz == gz) {
        outPath.push_back(validGoal);
        return true;
    }

    struct SearchNode {
        int x, z;
        float g, f;
        int parentX, parentZ;
    };

    struct CompareF {
        bool operator()(const SearchNode& a, const SearchNode& b) const {
            return a.f > b.f;
        }
    };

    std::priority_queue<SearchNode, std::vector<SearchNode>, CompareF> openSet;
    std::vector<float> gScore(m_nodes.size(), 1e9f);
    std::vector<int> parentMapX(m_nodes.size(), -1);
    std::vector<int> parentMapZ(m_nodes.size(), -1);
    std::vector<bool> closedSet(m_nodes.size(), false);

    auto Heuristic = [](int x1, int z1, int x2, int z2) -> float {
        float dx = static_cast<float>(x1 - x2);
        float dz = static_cast<float>(z1 - z2);
        return std::sqrt(dx * dx + dz * dz);
    };

    size_t startIdx = GetNodeIndex(sx, sz);
    gScore[startIdx] = 0.0f;
    openSet.push({ sx, sz, 0.0f, Heuristic(sx, sz, gx, gz), -1, -1 });

    static const int dirX[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };
    static const int dirZ[8] = { 1, 0, -1, 0, 1, -1, 1, -1 };
    static const float moveCost[8] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.4142f, 1.4142f, 1.4142f, 1.4142f };

    bool goalReached = false;

    while (!openSet.empty()) {
        SearchNode current = openSet.top();
        openSet.pop();

        size_t curIdx = GetNodeIndex(current.x, current.z);
        if (closedSet[curIdx]) continue;
        closedSet[curIdx] = true;

        if (current.x == gx && current.z == gz) {
            goalReached = true;
            break;
        }

        for (int i = 0; i < 8; ++i) {
            int nx = current.x + dirX[i];
            int nz = current.z + dirZ[i];

            if (!IsValidCoord(nx, nz)) continue;
            size_t nIdx = GetNodeIndex(nx, nz);

            if (!(m_nodes[nIdx].flags & NavCell_Walkable) || closedSet[nIdx]) {
                continue;
            }

            // Diagonal corner-cut prevention: both adjacent orthogonals must be walkable
            if (i >= 4) {
                int adj1X = current.x + dirX[i];
                int adj1Z = current.z;
                int adj2X = current.x;
                int adj2Z = current.z + dirZ[i];
                if (!IsValidCoord(adj1X, adj1Z) || !(m_nodes[GetNodeIndex(adj1X, adj1Z)].flags & NavCell_Walkable) ||
                    !IsValidCoord(adj2X, adj2Z) || !(m_nodes[GetNodeIndex(adj2X, adj2Z)].flags & NavCell_Walkable)) {
                    continue;
                }
            }

            float tentativeG = current.g + moveCost[i] * m_nodes[nIdx].costMultiplier;
            if (tentativeG < gScore[nIdx]) {
                gScore[nIdx] = tentativeG;
                parentMapX[nIdx] = current.x;
                parentMapZ[nIdx] = current.z;
                float f = tentativeG + Heuristic(nx, nz, gx, gz);
                openSet.push({ nx, nz, tentativeG, f, current.x, current.z });
            }
        }
    }

    if (!goalReached) return false;

    // Reconstruct raw path from parents
    std::vector<XMFLOAT3> rawPath;
    int currX = gx;
    int currZ = gz;

    while (currX != -1 && currZ != -1) {
        size_t idx = GetNodeIndex(currX, currZ);
        rawPath.push_back(XMFLOAT3(m_nodes[idx].worldX, m_nodes[idx].worldY, m_nodes[idx].worldZ));
        if (currX == sx && currZ == sz) break;
        int px = parentMapX[idx];
        int pz = parentMapZ[idx];
        currX = px;
        currZ = pz;
    }

    std::reverse(rawPath.begin(), rawPath.end());
    if (rawPath.empty()) return false;

    // Replace endpoints with exact start/target
    rawPath.front() = validStart;
    rawPath.back()  = validGoal;

    // Smooth path with string pulling
    SmoothPath(rawPath);

    outPath = std::move(rawPath);
    return true;
}

} // namespace EngineAI
