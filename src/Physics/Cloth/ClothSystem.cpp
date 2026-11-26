#include "ClothSystem.h"
#include "Core/Window.h"
#include "Renderer/Camera.h"
#include "Physics/PhysicsSystem.h"
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER) && !defined(ENGINE_PLATFORM_PS4)
#include <imgui.h>
#endif
#include <cmath>
#include <algorithm>
#include <chrono>

namespace EnginePhysics {

// Helper math
inline DirectX::XMFLOAT3 Vec3Add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline DirectX::XMFLOAT3 Vec3Sub(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline DirectX::XMFLOAT3 Vec3Mul(const DirectX::XMFLOAT3& a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
inline float Vec3Dot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline DirectX::XMFLOAT3 Vec3Cross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
inline float Vec3LengthSq(const DirectX::XMFLOAT3& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}
inline float Vec3Length(const DirectX::XMFLOAT3& v) {
    return std::sqrt(Vec3LengthSq(v));
}
inline DirectX::XMFLOAT3 Vec3Normalize(const DirectX::XMFLOAT3& v) {
    float len = Vec3Length(v);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }
    return { 0.0f, 0.0f, 0.0f };
}

// Ray-triangle intersection (Möller–Trumbore)
static bool RayTriangleIntersect(const DirectX::XMFLOAT3& rayOrig, const DirectX::XMFLOAT3& rayDir,
                                 const DirectX::XMFLOAT3& v0, const DirectX::XMFLOAT3& v1, const DirectX::XMFLOAT3& v2,
                                 float& outT, float& outU, float& outV) {
    DirectX::XMFLOAT3 e1 = Vec3Sub(v1, v0);
    DirectX::XMFLOAT3 e2 = Vec3Sub(v2, v0);
    DirectX::XMFLOAT3 pvec = Vec3Cross(rayDir, e2);
    float det = Vec3Dot(e1, pvec);

    if (std::abs(det) < 1e-7f) return false;
    float invDet = 1.0f / det;

    DirectX::XMFLOAT3 tvec = Vec3Sub(rayOrig, v0);
    float u = Vec3Dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    DirectX::XMFLOAT3 qvec = Vec3Cross(tvec, e1);
    float v = Vec3Dot(rayDir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = Vec3Dot(e2, qvec) * invDet;
    if (t < 0.001f) return false;

    outT = t;
    outU = u;
    outV = v;
    return true;
}

// Distance from point to line segment
static float DistPointToSegmentSq(const DirectX::XMFLOAT3& p, const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, DirectX::XMFLOAT3& closestPt) {
    DirectX::XMFLOAT3 ab = Vec3Sub(b, a);
    DirectX::XMFLOAT3 ap = Vec3Sub(p, a);
    float abLenSq = Vec3LengthSq(ab);
    if (abLenSq < 1e-6f) {
        closestPt = a;
        return Vec3LengthSq(ap);
    }
    float t = (std::clamp)(Vec3Dot(ap, ab) / abLenSq, 0.0f, 1.0f);
    closestPt = Vec3Add(a, Vec3Mul(ab, t));
    return Vec3LengthSq(Vec3Sub(p, closestPt));
}

// =========================================================================================
// ClothObject Implementation
// =========================================================================================

ClothObject::ClothObject(uint32_t id, const std::string& name)
    : m_id(id), m_name(name) {
    ApplyMaterialPreset(ClothMaterialType::Cotton);
}

ClothObject::~ClothObject() {
}

void ClothObject::ApplyMaterialPreset(ClothMaterialType matType) {
    m_materialType = matType;
    switch (matType) {
        case ClothMaterialType::Silk:
            m_config.totalMass = 1.0f;
            m_config.stretchStiffness = 0.99f;
            m_config.shearStiffness = 0.70f;
            m_config.bendStiffness = 0.20f;
            m_config.damping = 0.992f;
            m_config.dragCoefficient = 0.09f;
            m_config.surfaceFriction = 0.15f;
            m_material.albedo = { 0.85f, 0.15f, 0.25f }; // Crimson silk
            m_material.roughness = 0.22f;
            m_material.metallic = 0.10f;
            m_material.reflectance = 0.65f;
            break;
        case ClothMaterialType::Cotton:
            m_config.totalMass = 2.5f;
            m_config.stretchStiffness = 0.96f;
            m_config.shearStiffness = 0.85f;
            m_config.bendStiffness = 0.55f;
            m_config.damping = 0.985f;
            m_config.dragCoefficient = 0.08f;
            m_config.surfaceFriction = 0.45f;
            m_material.albedo = { 0.88f, 0.88f, 0.85f }; // Off-white cotton
            m_material.roughness = 0.75f;
            m_material.metallic = 0.0f;
            m_material.reflectance = 0.35f;
            break;
        case ClothMaterialType::Velvet:
            m_config.totalMass = 3.5f;
            m_config.stretchStiffness = 0.97f;
            m_config.shearStiffness = 0.90f;
            m_config.bendStiffness = 0.70f;
            m_config.damping = 0.978f;
            m_config.dragCoefficient = 0.07f;
            m_config.surfaceFriction = 0.60f;
            m_material.albedo = { 0.25f, 0.05f, 0.35f }; // Royal velvet purple
            m_material.roughness = 0.85f;
            m_material.metallic = 0.05f;
            m_material.reflectance = 0.50f;
            break;
        case ClothMaterialType::Denim:
            m_config.totalMass = 4.0f;
            m_config.stretchStiffness = 0.98f;
            m_config.shearStiffness = 0.92f;
            m_config.bendStiffness = 0.80f;
            m_config.damping = 0.975f;
            m_config.dragCoefficient = 0.06f;
            m_config.surfaceFriction = 0.55f;
            m_material.albedo = { 0.15f, 0.30f, 0.55f }; // Indigo denim
            m_material.roughness = 0.80f;
            m_material.metallic = 0.0f;
            m_material.reflectance = 0.30f;
            break;
        case ClothMaterialType::Leather:
            m_config.totalMass = 5.0f;
            m_config.stretchStiffness = 0.99f;
            m_config.shearStiffness = 0.95f;
            m_config.bendStiffness = 0.90f;
            m_config.damping = 0.965f;
            m_config.dragCoefficient = 0.05f;
            m_config.surfaceFriction = 0.50f;
            m_material.albedo = { 0.45f, 0.25f, 0.12f }; // Brown leather
            m_material.roughness = 0.45f;
            m_material.metallic = 0.0f;
            m_material.reflectance = 0.40f;
            break;
        case ClothMaterialType::GoldThread:
            m_config.totalMass = 3.0f;
            m_config.stretchStiffness = 0.98f;
            m_config.shearStiffness = 0.88f;
            m_config.bendStiffness = 0.60f;
            m_config.damping = 0.980f;
            m_config.dragCoefficient = 0.07f;
            m_config.surfaceFriction = 0.30f;
            m_material.albedo = { 1.0f, 0.78f, 0.28f }; // Gold brocade
            m_material.roughness = 0.30f;
            m_material.metallic = 0.85f;
            m_material.reflectance = 0.90f;
            break;
        case ClothMaterialType::PlasticSheet:
            m_config.totalMass = 1.2f;
            m_config.stretchStiffness = 0.99f;
            m_config.shearStiffness = 0.85f;
            m_config.bendStiffness = 0.40f;
            m_config.damping = 0.988f;
            m_config.dragCoefficient = 0.12f;
            m_config.surfaceFriction = 0.20f;
            m_material.albedo = { 0.85f, 0.92f, 0.98f }; // Translucent plastic
            m_material.roughness = 0.15f;
            m_material.metallic = 0.0f;
            m_material.reflectance = 0.70f;
            break;
        default:
            break;
    }
}

void ClothObject::SetPosition(const DirectX::XMFLOAT3& pos) {
    DirectX::XMFLOAT3 delta = Vec3Sub(pos, m_worldPosition);
    m_worldPosition = pos;
    for (auto& p : m_particles) {
        p.position = Vec3Add(p.position, delta);
        p.prevPosition = Vec3Add(p.prevPosition, delta);
        p.predictedPosition = Vec3Add(p.predictedPosition, delta);
        p.initialPosition = Vec3Add(p.initialPosition, delta);
    }
}

void ClothObject::SetRotation(const DirectX::XMFLOAT3& rot) {
    m_worldRotation = rot;
}

void ClothObject::CreateGrid(const DirectX::XMFLOAT3& centerPos, const DirectX::XMFLOAT3& rotationEuler,
                             float width, float height, int rows, int cols, int pinMode) {
    m_worldPosition = centerPos;
    m_worldRotation = rotationEuler;
    m_config.width = width;
    m_config.height = height;
    m_config.numRows = (std::max)(rows, 2);
    m_config.numCols = (std::max)(cols, 2);

    m_particles.clear();
    m_constraints.clear();
    m_triangles.clear();
    m_indices.clear();
    m_renderVertices.clear();

    int totalParticles = m_config.numRows * m_config.numCols;
    m_particles.resize(totalParticles);
    m_renderVertices.resize(totalParticles);

    float particleMass = m_config.totalMass / (float)totalParticles;
    float invMass = (particleMass > 0.0f) ? (1.0f / particleMass) : 1.0f;

    using namespace DirectX;
    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(rotationEuler.x),
        XMConvertToRadians(rotationEuler.y),
        XMConvertToRadians(rotationEuler.z)
    );

    float dx = width / (float)(m_config.numCols - 1);
    float dy = height / (float)(m_config.numRows - 1);

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    for (int r = 0; r < m_config.numRows; ++r) {
        for (int c = 0; c < m_config.numCols; ++c) {
            int idx = r * m_config.numCols + c;
            ClothParticle& p = m_particles[idx];

            float localX = (float)c * dx - halfW;
            float localY = halfH - (float)r * dy; // Top to bottom
            float localZ = 0.0f;

            XMVECTOR localPos = XMVectorSet(localX, localY, localZ, 1.0f);
            XMVECTOR worldPos = XMVector3Transform(localPos, rotMat);
            XMFLOAT3 worldPosF;
            XMStoreFloat3(&worldPosF, worldPos);

            p.position = Vec3Add(centerPos, worldPosF);
            p.prevPosition = p.position;
            p.predictedPosition = p.position;
            p.initialPosition = p.position;
            p.initialLocalOffset = { localX, localY, localZ };
            p.velocity = { 0.0f, 0.0f, 0.0f };
            p.uv = { (float)c / (float)(m_config.numCols - 1), (float)r / (float)(m_config.numRows - 1) };
            p.invMass = invMass;
            p.isPinned = false;
            p.isGrabbed = false;
            p.normal = { 0.0f, 0.0f, 1.0f };

            Renderer::Vertex& v = m_renderVertices[idx];
            v.position = p.position;
            v.normal = p.normal;
            v.color = XMFLOAT4(0.85f, 0.85f, p.uv.x, p.uv.y);
        }
    }

    // Apply Pin Mode
    switch (pinMode) {
        case 1: PinTopEdge(); break;
        case 2: PinFourCorners(); break;
        case 3: PinLeftEdge(); break;
        case 4: PinTwoTopCorners(); break;
        case 5: PinAllPerimeter(); break;
        default: break;
    }

    // Generate Constraints
    // 1. Structural Constraints (Horizontal & Vertical)
    for (int r = 0; r < m_config.numRows; ++r) {
        for (int c = 0; c < m_config.numCols; ++c) {
            int idx = r * m_config.numCols + c;

            // Horizontal
            if (c + 1 < m_config.numCols) {
                int rightIdx = r * m_config.numCols + (c + 1);
                ClothConstraint cstr;
                cstr.p1 = idx;
                cstr.p2 = rightIdx;
                cstr.restLength = Vec3Length(Vec3Sub(m_particles[idx].position, m_particles[rightIdx].position));
                cstr.stiffness = m_config.stretchStiffness;
                cstr.type = ClothConstraintType::Structural;
                cstr.maxStretchRatio = m_config.tearThreshold;
                m_constraints.push_back(cstr);
            }

            // Vertical
            if (r + 1 < m_config.numRows) {
                int bottomIdx = (r + 1) * m_config.numCols + c;
                ClothConstraint cstr;
                cstr.p1 = idx;
                cstr.p2 = bottomIdx;
                cstr.restLength = Vec3Length(Vec3Sub(m_particles[idx].position, m_particles[bottomIdx].position));
                cstr.stiffness = m_config.stretchStiffness;
                cstr.type = ClothConstraintType::Structural;
                cstr.maxStretchRatio = m_config.tearThreshold;
                m_constraints.push_back(cstr);
            }
        }
    }

    // 2. Shear Constraints (Diagonals)
    for (int r = 0; r < m_config.numRows - 1; ++r) {
        for (int c = 0; c < m_config.numCols - 1; ++c) {
            int i00 = r * m_config.numCols + c;
            int i11 = (r + 1) * m_config.numCols + (c + 1);
            int i01 = r * m_config.numCols + (c + 1);
            int i10 = (r + 1) * m_config.numCols + c;

            // Diagonal 1: (0,0) -> (1,1)
            ClothConstraint cstr1;
            cstr1.p1 = i00;
            cstr1.p2 = i11;
            cstr1.restLength = Vec3Length(Vec3Sub(m_particles[i00].position, m_particles[i11].position));
            cstr1.stiffness = m_config.shearStiffness;
            cstr1.type = ClothConstraintType::Shear;
            cstr1.maxStretchRatio = m_config.tearThreshold * 1.15f;
            m_constraints.push_back(cstr1);

            // Diagonal 2: (0,1) -> (1,0)
            ClothConstraint cstr2;
            cstr2.p1 = i01;
            cstr2.p2 = i10;
            cstr2.restLength = Vec3Length(Vec3Sub(m_particles[i01].position, m_particles[i10].position));
            cstr2.stiffness = m_config.shearStiffness;
            cstr2.type = ClothConstraintType::Shear;
            cstr2.maxStretchRatio = m_config.tearThreshold * 1.15f;
            m_constraints.push_back(cstr2);
        }
    }

    // 3. Bending Constraints (Step = 2)
    for (int r = 0; r < m_config.numRows; ++r) {
        for (int c = 0; c < m_config.numCols; ++c) {
            int idx = r * m_config.numCols + c;

            // Horizontal Bend (c to c+2)
            if (c + 2 < m_config.numCols) {
                int right2 = r * m_config.numCols + (c + 2);
                ClothConstraint cstr;
                cstr.p1 = idx;
                cstr.p2 = right2;
                cstr.restLength = Vec3Length(Vec3Sub(m_particles[idx].position, m_particles[right2].position));
                cstr.stiffness = m_config.bendStiffness;
                cstr.type = ClothConstraintType::Bending;
                cstr.maxStretchRatio = m_config.tearThreshold * 1.3f;
                m_constraints.push_back(cstr);
            }

            // Vertical Bend (r to r+2)
            if (r + 2 < m_config.numRows) {
                int bottom2 = (r + 2) * m_config.numCols + c;
                ClothConstraint cstr;
                cstr.p1 = idx;
                cstr.p2 = bottom2;
                cstr.restLength = Vec3Length(Vec3Sub(m_particles[idx].position, m_particles[bottom2].position));
                cstr.stiffness = m_config.bendStiffness;
                cstr.type = ClothConstraintType::Bending;
                cstr.maxStretchRatio = m_config.tearThreshold * 1.3f;
                m_constraints.push_back(cstr);
            }
        }
    }

    // Generate Triangles and Indices
    for (int r = 0; r < m_config.numRows - 1; ++r) {
        for (int c = 0; c < m_config.numCols - 1; ++c) {
            uint32_t i0 = (uint32_t)(r * m_config.numCols + c);
            uint32_t i1 = (uint32_t)(r * m_config.numCols + (c + 1));
            uint32_t i2 = (uint32_t)((r + 1) * m_config.numCols + (c + 1));
            uint32_t i3 = (uint32_t)((r + 1) * m_config.numCols + c);

            // Triangle 1: i0 -> i1 -> i2
            ClothTriangle tri1;
            tri1.i0 = i0; tri1.i1 = i1; tri1.i2 = i2;
            m_triangles.push_back(tri1);
            m_indices.push_back(i0);
            m_indices.push_back(i1);
            m_indices.push_back(i2);

            // Triangle 2: i0 -> i2 -> i3
            ClothTriangle tri2;
            tri2.i0 = i0; tri2.i1 = i2; tri2.i2 = i3;
            m_triangles.push_back(tri2);
            m_indices.push_back(i0);
            m_indices.push_back(i2);
            m_indices.push_back(i3);
        }
    }

    RecalculateNormals();
    m_topologyDirty = true;
}

void ClothObject::SetPin(uint32_t particleIndex, bool pinned) {
    if (particleIndex < m_particles.size()) {
        m_particles[particleIndex].isPinned = pinned;
        if (pinned) {
            m_particles[particleIndex].invMass = 0.0f;
            m_particles[particleIndex].velocity = { 0.0f, 0.0f, 0.0f };
        } else {
            float totalMass = m_config.totalMass;
            m_particles[particleIndex].invMass = (totalMass > 0.0f) ? ((float)m_particles.size() / totalMass) : 1.0f;
        }
    }
}

void ClothObject::TogglePin(uint32_t particleIndex) {
    if (particleIndex < m_particles.size()) {
        SetPin(particleIndex, !m_particles[particleIndex].isPinned);
    }
}

void ClothObject::PinTopEdge() {
    UnpinAll();
    for (int c = 0; c < m_config.numCols; ++c) {
        SetPin(c, true);
    }
}

void ClothObject::PinLeftEdge() {
    UnpinAll();
    for (int r = 0; r < m_config.numRows; ++r) {
        SetPin(r * m_config.numCols, true);
    }
}

void ClothObject::PinCorners() {
    PinFourCorners();
}

void ClothObject::PinFourCorners() {
    UnpinAll();
    int rows = m_config.numRows;
    int cols = m_config.numCols;
    SetPin(0, true);
    SetPin(cols - 1, true);
    SetPin((rows - 1) * cols, true);
    SetPin(rows * cols - 1, true);
}

void ClothObject::PinTwoTopCorners() {
    UnpinAll();
    int cols = m_config.numCols;
    SetPin(0, true);
    SetPin(cols - 1, true);
}

void ClothObject::PinAllPerimeter() {
    UnpinAll();
    int rows = m_config.numRows;
    int cols = m_config.numCols;
    for (int c = 0; c < cols; ++c) {
        SetPin(c, true);
        SetPin((rows - 1) * cols + c, true);
    }
    for (int r = 0; r < rows; ++r) {
        SetPin(r * cols, true);
        SetPin(r * cols + (cols - 1), true);
    }
}

void ClothObject::UnpinAll() {
    for (size_t i = 0; i < m_particles.size(); ++i) {
        SetPin((uint32_t)i, false);
    }
}

void ClothObject::Reset() {
    for (auto& p : m_particles) {
        p.position = p.initialPosition;
        p.prevPosition = p.position;
        p.predictedPosition = p.position;
        p.velocity = { 0.0f, 0.0f, 0.0f };
        p.isGrabbed = false;
    }
    for (auto& c : m_constraints) {
        c.isBroken = false;
    }
    for (auto& t : m_triangles) {
        t.isDeleted = false;
    }
    RecalculateNormals();
    m_topologyDirty = true;
}

void ClothObject::AddForce(const DirectX::XMFLOAT3& force) {
    for (auto& p : m_particles) {
        if (!p.isPinned && p.invMass > 0.0f) {
            p.velocity = Vec3Add(p.velocity, Vec3Mul(force, p.invMass));
        }
    }
}

void ClothObject::AddImpulseAtParticle(uint32_t particleIndex, const DirectX::XMFLOAT3& impulse) {
    if (particleIndex < m_particles.size() && !m_particles[particleIndex].isPinned) {
        m_particles[particleIndex].velocity = Vec3Add(m_particles[particleIndex].velocity, Vec3Mul(impulse, m_particles[particleIndex].invMass));
    }
}

void ClothObject::AddExplosionForce(const DirectX::XMFLOAT3& origin, float radius, float strength) {
    float radiusSq = radius * radius;
    for (auto& p : m_particles) {
        if (p.isPinned) continue;
        DirectX::XMFLOAT3 diff = Vec3Sub(p.position, origin);
        float distSq = Vec3LengthSq(diff);
        if (distSq < radiusSq && distSq > 1e-4f) {
            float dist = std::sqrt(distSq);
            float factor = (1.0f - dist / radius) * strength * p.invMass;
            DirectX::XMFLOAT3 dir = Vec3Mul(diff, 1.0f / dist);
            dir.y += 0.25f; // Add upward lift
            p.velocity = Vec3Add(p.velocity, Vec3Mul(dir, factor));
        }
    }
}

void ClothObject::TearConstraint(size_t constraintIndex) {
    if (constraintIndex < m_constraints.size()) {
        m_constraints[constraintIndex].isBroken = true;
    }
}

bool ClothObject::SliceByRay(const DirectX::XMFLOAT3& rayStart, const DirectX::XMFLOAT3& rayDir, float maxDistance) {
    bool slicedAny = false;
    for (size_t i = 0; i < m_constraints.size(); ++i) {
        if (m_constraints[i].isBroken) continue;
        const ClothParticle& p1 = m_particles[m_constraints[i].p1];
        const ClothParticle& p2 = m_particles[m_constraints[i].p2];

        DirectX::XMFLOAT3 closestRayPt;
        DirectX::XMFLOAT3 midEdge = Vec3Mul(Vec3Add(p1.position, p2.position), 0.5f);
        float dToRay = DistPointToSegmentSq(midEdge, rayStart, Vec3Add(rayStart, Vec3Mul(rayDir, maxDistance)), closestRayPt);
        if (dToRay < 0.04f * 0.04f) {
            m_constraints[i].isBroken = true;
            slicedAny = true;
        }
    }
    if (slicedAny) {
        m_topologyDirty = true;
    }
    return slicedAny;
}

bool ClothObject::Raycast(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir,
                          float& outDist, uint32_t& outClosestParticle, DirectX::XMFLOAT3& outHitPoint) {
    float nearestT = 1e9f;
    bool hasHit = false;
    uint32_t bestParticle = 0;

    for (const auto& tri : m_triangles) {
        if (tri.isDeleted) continue;
        const DirectX::XMFLOAT3& v0 = m_particles[tri.i0].position;
        const DirectX::XMFLOAT3& v1 = m_particles[tri.i1].position;
        const DirectX::XMFLOAT3& v2 = m_particles[tri.i2].position;

        float t, u, v;
        if (RayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t, u, v)) {
            if (t < nearestT) {
                nearestT = t;
                hasHit = true;
                outDist = t;
                outHitPoint = Vec3Add(rayOrigin, Vec3Mul(rayDir, t));

                // Find closest vertex of the hit triangle
                float w = 1.0f - u - v;
                if (w >= u && w >= v) bestParticle = tri.i0;
                else if (u >= w && u >= v) bestParticle = tri.i1;
                else bestParticle = tri.i2;
            }
        }
    }

    if (hasHit) {
        outClosestParticle = bestParticle;
    }
    return hasHit;
}

// -----------------------------------------------------------------------------------------
// XPBD Physics Step & Constraint Solving
// -----------------------------------------------------------------------------------------

void ClothObject::ApplyAerodynamics(const DirectX::XMFLOAT3& windVelocity, float dt) {
    if (!m_config.enableWind) return;

    for (auto& tri : m_triangles) {
        if (tri.isDeleted) continue;
        ClothParticle& p0 = m_particles[tri.i0];
        ClothParticle& p1 = m_particles[tri.i1];
        ClothParticle& p2 = m_particles[tri.i2];

        // Triangle center & velocity
        DirectX::XMFLOAT3 vTri = Vec3Mul(Vec3Add(p0.velocity, Vec3Add(p1.velocity, p2.velocity)), 0.33333f);
        DirectX::XMFLOAT3 vRel = Vec3Sub(windVelocity, vTri);
        float vRelLen = Vec3Length(vRel);
        if (vRelLen < 1e-4f) continue;

        DirectX::XMFLOAT3 vRelDir = Vec3Mul(vRel, 1.0f / vRelLen);

        // Compute face normal & area
        DirectX::XMFLOAT3 e1 = Vec3Sub(p1.position, p0.position);
        DirectX::XMFLOAT3 e2 = Vec3Sub(p2.position, p0.position);
        DirectX::XMFLOAT3 crossN = Vec3Cross(e1, e2);
        float area2 = Vec3Length(crossN);
        if (area2 < 1e-6f) continue;

        DirectX::XMFLOAT3 n = Vec3Mul(crossN, 1.0f / area2);
        float area = area2 * 0.5f;

        float nDotV = Vec3Dot(n, vRelDir);
        float vRelSpeedSq = vRelLen * vRelLen;

        // Aerodynamic Drag Force: F_drag = 0.5 * rho * C_d * area * (v_rel . n) * n
        float dragMag = 0.5f * 1.225f * m_config.dragCoefficient * area * vRelSpeedSq * nDotV;
        DirectX::XMFLOAT3 fDrag = Vec3Mul(n, dragMag);

        // Aerodynamic Lift Force: F_lift = 0.5 * rho * C_l * area * ((n x v_rel) x v_rel)
        DirectX::XMFLOAT3 liftDir = Vec3Cross(Vec3Cross(n, vRelDir), vRelDir);
        float liftMag = 0.5f * 1.225f * m_config.liftCoefficient * area * vRelSpeedSq * std::abs(nDotV);
        DirectX::XMFLOAT3 fLift = Vec3Mul(liftDir, liftMag);

        DirectX::XMFLOAT3 fTotal = Vec3Add(fDrag, fLift);
        DirectX::XMFLOAT3 fPerVert = Vec3Mul(fTotal, 0.33333f);

        if (!p0.isPinned) p0.velocity = Vec3Add(p0.velocity, Vec3Mul(fPerVert, p0.invMass * dt));
        if (!p1.isPinned) p1.velocity = Vec3Add(p1.velocity, Vec3Mul(fPerVert, p1.invMass * dt));
        if (!p2.isPinned) p2.velocity = Vec3Add(p2.velocity, Vec3Mul(fPerVert, p2.invMass * dt));
    }
}

void ClothObject::SolveDistanceConstraints() {
    for (size_t i = 0; i < m_constraints.size(); ++i) {
        ClothConstraint& c = m_constraints[i];
        if (c.isBroken) continue;

        ClothParticle& p1 = m_particles[c.p1];
        ClothParticle& p2 = m_particles[c.p2];

        float w1 = p1.invMass;
        float w2 = p2.invMass;
        float wSum = w1 + w2;
        if (wSum < 1e-7f) continue;

        DirectX::XMFLOAT3 delta = Vec3Sub(p1.predictedPosition, p2.predictedPosition);
        float dist = Vec3Length(delta);
        if (dist < 1e-6f) continue;

        // Dynamic tearing detection
        if (m_config.enableTearing && dist > c.restLength * c.maxStretchRatio) {
            c.isBroken = true;
            m_topologyDirty = true;
            continue;
        }

        float error = (dist - c.restLength);
        float correctionMag = (error / wSum) * c.stiffness;
        DirectX::XMFLOAT3 dir = Vec3Mul(delta, 1.0f / dist);

        if (!p1.isPinned && !p1.isGrabbed) {
            p1.predictedPosition = Vec3Sub(p1.predictedPosition, Vec3Mul(dir, w1 * correctionMag));
        }
        if (!p2.isPinned && !p2.isGrabbed) {
            p2.predictedPosition = Vec3Add(p2.predictedPosition, Vec3Mul(dir, w2 * correctionMag));
        }
    }
}

void ClothObject::SolveCollisions(const std::vector<ClothCollider>& colliders) {
    float thickness = m_config.collisionThickness;
    float friction = m_config.surfaceFriction;

    for (auto& p : m_particles) {
        if (p.isPinned || p.isGrabbed) continue;

        for (const auto& col : colliders) {
            switch (col.type) {
                case ClothColliderType::Plane: {
                    // Infinite horizontal floor plane at col.position.y
                    float floorY = col.position.y + thickness;
                    if (p.predictedPosition.y < floorY) {
                        p.predictedPosition.y = floorY;
                        // Apply friction to horizontal velocity
                        p.predictedPosition.x = p.position.x + (p.predictedPosition.x - p.position.x) * (1.0f - friction);
                        p.predictedPosition.z = p.position.z + (p.predictedPosition.z - p.position.z) * (1.0f - friction);
                    }
                    break;
                }
                case ClothColliderType::Sphere: {
                    DirectX::XMFLOAT3 diff = Vec3Sub(p.predictedPosition, col.position);
                    float dist = Vec3Length(diff);
                    float targetDist = col.radius + thickness;
                    if (dist < targetDist) {
                        DirectX::XMFLOAT3 normal = (dist > 1e-5f) ? Vec3Mul(diff, 1.0f / dist) : DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
                        p.predictedPosition = Vec3Add(col.position, Vec3Mul(normal, targetDist));
                        // Tangential surface friction
                        DirectX::XMFLOAT3 tangentDisp = Vec3Sub(p.predictedPosition, p.position);
                        DirectX::XMFLOAT3 normalDisp = Vec3Mul(normal, Vec3Dot(tangentDisp, normal));
                        DirectX::XMFLOAT3 tangOnly = Vec3Sub(tangentDisp, normalDisp);
                        p.predictedPosition = Vec3Sub(p.predictedPosition, Vec3Mul(tangOnly, friction));
                    }
                    break;
                }
                case ClothColliderType::Box: {
                    // OBB collision using local coordinate transformation
                    using namespace DirectX;
                    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(
                        XMConvertToRadians(col.rotation.x),
                        XMConvertToRadians(col.rotation.y),
                        XMConvertToRadians(col.rotation.z)
                    );
                    XMMATRIX invRotMat = XMMatrixTranspose(rotMat);

                    DirectX::XMFLOAT3 relPos = Vec3Sub(p.predictedPosition, col.position);
                    XMVECTOR relVec = XMVectorSet(relPos.x, relPos.y, relPos.z, 0.0f);
                    XMVECTOR localVec = XMVector3Transform(relVec, invRotMat);
                    XMFLOAT3 localPos;
                    XMStoreFloat3(&localPos, localVec);

                    float hx = col.size.x + thickness;
                    float hy = col.size.y + thickness;
                    float hz = col.size.z + thickness;

                    if (std::abs(localPos.x) < hx && std::abs(localPos.y) < hy && std::abs(localPos.z) < hz) {
                        // Point inside box: find closest face to push out
                        float dx = hx - std::abs(localPos.x);
                        float dy = hy - std::abs(localPos.y);
                        float dz = hz - std::abs(localPos.z);

                        XMFLOAT3 localNormal = { 0.0f, 0.0f, 0.0f };
                        if (dx < dy && dx < dz) {
                            localPos.x = (localPos.x > 0.0f) ? hx : -hx;
                            localNormal.x = (localPos.x > 0.0f) ? 1.0f : -1.0f;
                        } else if (dy < dz) {
                            localPos.y = (localPos.y > 0.0f) ? hy : -hy;
                            localNormal.y = (localPos.y > 0.0f) ? 1.0f : -1.0f;
                        } else {
                            localPos.z = (localPos.z > 0.0f) ? hz : -hz;
                            localNormal.z = (localPos.z > 0.0f) ? 1.0f : -1.0f;
                        }

                        XMVECTOR pushedLocal = XMVectorSet(localPos.x, localPos.y, localPos.z, 0.0f);
                        XMVECTOR pushedWorld = XMVector3Transform(pushedLocal, rotMat);
                        XMFLOAT3 pushedWorldF;
                        XMStoreFloat3(&pushedWorldF, pushedWorld);

                        p.predictedPosition = Vec3Add(col.position, pushedWorldF);
                    }
                    break;
                }
                case ClothColliderType::Capsule: {
                    float halfH = col.height * 0.5f;
                    DirectX::XMFLOAT3 capA = { col.position.x, col.position.y - halfH, col.position.z };
                    DirectX::XMFLOAT3 capB = { col.position.x, col.position.y + halfH, col.position.z };
                    DirectX::XMFLOAT3 closestPt;
                    float distSq = DistPointToSegmentSq(p.predictedPosition, capA, capB, closestPt);
                    float targetDist = col.radius + thickness;
                    if (distSq < targetDist * targetDist) {
                        float dist = std::sqrt(distSq);
                        DirectX::XMFLOAT3 normal = (dist > 1e-5f) ? Vec3Mul(Vec3Sub(p.predictedPosition, closestPt), 1.0f / dist) : DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
                        p.predictedPosition = Vec3Add(closestPt, Vec3Mul(normal, targetDist));
                    }
                    break;
                }
            }
        }
    }
}

void ClothObject::SolveSelfCollisions() {
    if (!m_config.enableSelfCollision) return;

    float r = m_config.selfCollisionRadius;
    float r2 = r * r;
    size_t count = m_particles.size();

    for (size_t i = 0; i < count; ++i) {
        if (m_particles[i].isPinned) continue;
        for (size_t j = i + 1; j < count; ++j) {
            DirectX::XMFLOAT3 diff = Vec3Sub(m_particles[i].predictedPosition, m_particles[j].predictedPosition);
            float distSq = Vec3LengthSq(diff);
            if (distSq < r2 && distSq > 1e-6f) {
                float dist = std::sqrt(distSq);
                float overlap = (r - dist) * 0.5f;
                DirectX::XMFLOAT3 n = Vec3Mul(diff, 1.0f / dist);

                if (!m_particles[i].isPinned) {
                    m_particles[i].predictedPosition = Vec3Add(m_particles[i].predictedPosition, Vec3Mul(n, overlap));
                }
                if (!m_particles[j].isPinned) {
                    m_particles[j].predictedPosition = Vec3Sub(m_particles[j].predictedPosition, Vec3Mul(n, overlap));
                }
            }
        }
    }
}

void ClothObject::StepPhysics(float dt, const DirectX::XMFLOAT3& gravity, const DirectX::XMFLOAT3& windVelocity,
                             const std::vector<ClothCollider>& colliders) {
    if (m_particles.empty()) return;

    int subSteps = (std::clamp)(m_config.subSteps, 1, 8);
    float dtSub = dt / (float)subSteps;
    float dtSubSq = dtSub * dtSub;
    DirectX::XMFLOAT3 effGravity = Vec3Mul(gravity, m_config.gravityMultiplier);

    for (int step = 0; step < subSteps; ++step) {
        // 1. Aerodynamics Force Integration
        ApplyAerodynamics(windVelocity, dtSub);

        // 2. Symplectic Euler / Verlet Predictor
        for (auto& p : m_particles) {
            if (p.isPinned) {
                p.predictedPosition = p.position;
                p.velocity = { 0.0f, 0.0f, 0.0f };
                continue;
            }

            // Velocity damping
            p.velocity = Vec3Mul(p.velocity, m_config.damping);

            // Integrate acceleration: a = gravity
            p.velocity = Vec3Add(p.velocity, Vec3Mul(effGravity, dtSub));

            // Position prediction
            p.predictedPosition = Vec3Add(p.position, Vec3Mul(p.velocity, dtSub));
        }

        // 3. XPBD Constraint Projection Iterations
        int iters = (std::clamp)(m_config.solverIterations, 1, 30);
        for (int iter = 0; iter < iters; ++iter) {
            SolveDistanceConstraints();
            SolveCollisions(colliders);
            SolveSelfCollisions();
        }

        // 4. Update Velocity and Final Positions
        for (auto& p : m_particles) {
            if (!p.isPinned) {
                p.velocity = Vec3Mul(Vec3Sub(p.predictedPosition, p.position), 1.0f / dtSub);
                p.prevPosition = p.position;
                p.position = p.predictedPosition;
            }
        }
    }

    RecalculateNormals();
}

void ClothObject::RecalculateNormals() {
    for (auto& p : m_particles) {
        p.normal = { 0.0f, 0.0f, 0.0f };
    }

    for (auto& tri : m_triangles) {
        if (tri.isDeleted) continue;
        const DirectX::XMFLOAT3& v0 = m_particles[tri.i0].position;
        const DirectX::XMFLOAT3& v1 = m_particles[tri.i1].position;
        const DirectX::XMFLOAT3& v2 = m_particles[tri.i2].position;

        DirectX::XMFLOAT3 e1 = Vec3Sub(v1, v0);
        DirectX::XMFLOAT3 e2 = Vec3Sub(v2, v0);
        DirectX::XMFLOAT3 fn = Vec3Cross(e1, e2);
        float area2 = Vec3Length(fn);

        tri.area = area2 * 0.5f;
        if (area2 > 1e-6f) {
            tri.normal = Vec3Mul(fn, 1.0f / area2);
            // Area-weighted normal accumulation
            m_particles[tri.i0].normal = Vec3Add(m_particles[tri.i0].normal, fn);
            m_particles[tri.i1].normal = Vec3Add(m_particles[tri.i1].normal, fn);
            m_particles[tri.i2].normal = Vec3Add(m_particles[tri.i2].normal, fn);
        }
    }

    for (size_t i = 0; i < m_particles.size(); ++i) {
        m_particles[i].normal = Vec3Normalize(m_particles[i].normal);
        if (i < m_renderVertices.size()) {
            m_renderVertices[i].position = m_particles[i].position;
            m_renderVertices[i].normal = m_particles[i].normal;
            float pinTint = m_particles[i].isPinned ? 0.35f : 1.0f;
            m_renderVertices[i].color = DirectX::XMFLOAT4(pinTint, pinTint, m_particles[i].uv.x, m_particles[i].uv.y);
        }
    }
}

void ClothObject::SyncToRenderer(Renderer* renderer) {
    if (!renderer || m_renderVertices.empty() || m_indices.empty()) return;

    bool exists = false;
    if (m_dynamicModelId != 0) {
        for (const auto& dm : renderer->GetDynamicModels()) {
            if (dm.modelId == m_dynamicModelId) {
                exists = true;
                break;
            }
        }
    }

    if (!exists) {
        // Create DX12 Dynamic Model
        m_dynamicModelId = renderer->CreateDynamicClothModel(m_name, m_renderVertices, m_indices, m_material);
        m_topologyDirty = false;
    } else {
        if (m_topologyDirty) {
            renderer->UpdateDynamicModelTopology(m_dynamicModelId, m_renderVertices, m_indices);
            m_topologyDirty = false;
        } else {
            renderer->UpdateDynamicModelVertexBuffer(m_dynamicModelId, m_renderVertices);
        }
    }
}

// =========================================================================================
// ClothSystem Implementation
// =========================================================================================

ClothSystem& ClothSystem::Get() {
    static ClothSystem s_instance;
    return s_instance;
}

ClothSystem::ClothSystem() {
    UpdateWindFromAngles();
}

ClothSystem::~ClothSystem() {
}

void ClothSystem::UpdateWindFromAngles() {
    using namespace DirectX;
    float azRad = XMConvertToRadians(m_windAzimuth);
    float elRad = XMConvertToRadians(m_windElevation);
    m_windDirection.x = std::cos(elRad) * std::sin(azRad);
    m_windDirection.y = std::sin(elRad);
    m_windDirection.z = std::cos(elRad) * std::cos(azRad);
    m_windDirection = Vec3Normalize(m_windDirection);
}

DirectX::XMFLOAT3 ClothSystem::CalculateWindAt(const DirectX::XMFLOAT3& pos, float time) const {
    if (!m_windEnabled || m_windSpeed <= 0.0f) return { 0.0f, 0.0f, 0.0f };

    // Multi-octave gust & spatial turbulence synthesis
    float gust = std::sin(time * m_windGustFrequency * 2.0f + pos.x * 0.4f + pos.z * 0.4f);
    gust = (gust + 1.0f) * 0.5f; // 0..1
    float currentSpeed = m_windSpeed + gust * m_windGustStrength;

    // High frequency micro-turbulence fluctuations
    float turb1 = std::sin(time * 5.2f + pos.y * 1.5f + pos.x * 2.1f);
    float turb2 = std::cos(time * 4.1f + pos.z * 1.8f);
    float turb3 = std::sin(time * 6.7f + pos.x * 3.3f);

    DirectX::XMFLOAT3 turbVec = {
        turb1 * m_windTurbulence * 0.35f,
        turb2 * m_windTurbulence * 0.20f,
        turb3 * m_windTurbulence * 0.35f
    };

    DirectX::XMFLOAT3 mainWind = Vec3Mul(m_windDirection, currentSpeed);
    return Vec3Add(mainWind, turbVec);
}

bool ClothSystem::Initialize(Renderer* renderer) {
    LOG("ClothSystem::Initialize starting...");
    LOG("ClothSystem::Initialize SUCCESS!");
    return true;
}

void ClothSystem::Shutdown(Renderer* renderer) {
    ClearAllCloths(renderer);
}

void ClothSystem::CollectSceneColliders(Renderer* renderer) {
    m_colliders.clear();

    // 1. Static Floor Plane collider at y = 0.0
    ClothCollider floorCol;
    floorCol.type = ClothColliderType::Plane;
    floorCol.position = { 0.0f, 0.0f, 0.0f };
    floorCol.friction = 0.5f;
    m_colliders.push_back(floorCol);

    if (renderer) {
        // 2. Colliders from Scene Static Models
        for (const auto& m : renderer->GetModels()) {
            if (!m.visible || !m.hasCollision || m.name == "Floor") continue;

            ClothCollider col;
            col.position = m.position;
            col.rotation = m.rotation;
            col.friction = 0.4f;

            if (m.name.find("Sphere") != std::string::npos) {
                col.type = ClothColliderType::Sphere;
                col.radius = m.scale.x * 0.5f;
            } else {
                col.type = ClothColliderType::Box;
                col.size = { m.scale.x * 0.5f, m.scale.y * 0.5f, m.scale.z * 0.5f };
            }
            m_colliders.push_back(col);
        }

        // 3. Colliders from Dynamic Models
        for (const auto& dm : renderer->GetDynamicModels()) {
            if (!dm.visible || !dm.hasCollision) continue;
            // Avoid self-colliding with our own cloth mesh
            bool isCloth = false;
            for (const auto& cloth : m_cloths) {
                if (cloth->GetDynamicModelId() == dm.modelId) {
                    isCloth = true;
                    break;
                }
            }
            if (isCloth) continue;

            ClothCollider col;
            col.position = dm.position;
            col.rotation = dm.rotation;
            col.dynamic = true;
            col.friction = 0.4f;

            if (dm.name.find("Sphere") != std::string::npos) {
                col.type = ClothColliderType::Sphere;
                col.radius = dm.scale.x * 0.5f;
            } else {
                col.type = ClothColliderType::Box;
                col.size = { dm.scale.x * 0.5f, dm.scale.y * 0.5f, dm.scale.z * 0.5f };
            }
            m_colliders.push_back(col);
        }
    }

    // 4. Custom Colliders (e.g. Flagpoles, Pedestals)
    for (const auto& cc : m_customColliders) {
        m_colliders.push_back(cc);
    }
}

void ClothSystem::Update(float dt, Renderer* renderer, Window* window, Camera* camera) {
    if (!renderer) return;

    m_simulationTime += dt;
    CollectSceneColliders(renderer);

    // Interactive mouse / tool handling
    if (camera && window && !m_isPaused) {
        DirectX::XMFLOAT3 camPos = camera->GetPosition();
        DirectX::XMFLOAT3 camDir = camera->GetForwardVector();

#ifndef ENGINE_RELEASE
        bool isLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && window->IsCursorEnabled() && !ImGui::GetIO().WantCaptureMouse;
#else
        bool isLeftDown = false;
#endif
        bool isGrabKey = window->IsKeyDown('F') || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        bool isCutKey = window->IsKeyDown('X');
        bool isPinKey = window->IsKeyDown('P');

        // --- 1. Grab Tool ---
        if (m_interactionMode == ClothInteractionMode::Grab || isGrabKey) {
            if (isGrabKey || isLeftDown) {
                if (!m_isInteracting) {
                    // Raycast against all cloths
                    float bestDist = 1e9f;
                    uint32_t hitClothId = 0;
                    uint32_t hitParticle = 0;
                    DirectX::XMFLOAT3 hitPos;

                    for (auto& cloth : m_cloths) {
                        float dist;
                        uint32_t pIdx;
                        DirectX::XMFLOAT3 hp;
                        if (cloth->Raycast(camPos, camDir, dist, pIdx, hp) && dist < bestDist) {
                            bestDist = dist;
                            hitClothId = cloth->GetId();
                            hitParticle = pIdx;
                            hitPos = hp;
                        }
                    }

                    if (hitClothId != 0 && bestDist < 40.0f) {
                        m_grabbedClothId = hitClothId;
                        m_grabbedParticleIdx = hitParticle;
                        m_grabDistance = (std::clamp)(bestDist, 1.5f, 25.0f);
                        m_isInteracting = true;

                        auto cloth = GetClothById(m_grabbedClothId);
                        if (cloth && m_grabbedParticleIdx < cloth->GetParticles().size()) {
                            cloth->GetParticles()[m_grabbedParticleIdx].isGrabbed = true;
                        }
                    }
                } else {
                    // Update grabbed particle position to camera target point
                    auto cloth = GetClothById(m_grabbedClothId);
                    if (cloth && m_grabbedParticleIdx < cloth->GetParticles().size()) {
                        DirectX::XMFLOAT3 targetPos = Vec3Add(camPos, Vec3Mul(camDir, m_grabDistance));
                        auto& p = cloth->GetParticles()[m_grabbedParticleIdx];
                        p.predictedPosition = targetPos;
                        p.position = targetPos;
                        p.velocity = { 0.0f, 0.0f, 0.0f };
                    }
                }
            } else {
                if (m_isInteracting) {
                    auto cloth = GetClothById(m_grabbedClothId);
                    if (cloth && m_grabbedParticleIdx < cloth->GetParticles().size()) {
                        cloth->GetParticles()[m_grabbedParticleIdx].isGrabbed = false;
                    }
                    m_isInteracting = false;
                    m_grabbedClothId = 0;
                }
            }
        }

        // --- 2. Knife / Slicing Tool ---
        if (m_interactionMode == ClothInteractionMode::Knife || isCutKey) {
            if (isLeftDown || isCutKey) {
                for (auto& cloth : m_cloths) {
                    cloth->SliceByRay(camPos, camDir, 30.0f);
                }
            }
        }

        // --- 3. Pin / Unpin Brush Tool ---
        if (m_interactionMode == ClothInteractionMode::PinBrush || isPinKey) {
            static bool s_prevPinPress = false;
            bool currentPinPress = isLeftDown || isPinKey;
            if (currentPinPress && !s_prevPinPress) {
                for (auto& cloth : m_cloths) {
                    float dist;
                    uint32_t pIdx;
                    DirectX::XMFLOAT3 hp;
                    if (cloth->Raycast(camPos, camDir, dist, pIdx, hp) && dist < 30.0f) {
                        cloth->TogglePin(pIdx);
                        break;
                    }
                }
            }
            s_prevPinPress = currentPinPress;
        }
    }

    if (m_isPaused && !m_singleStep) return;

    auto tStart = std::chrono::high_resolution_clock::now();

    // Physics step for all active cloth objects
    DirectX::XMFLOAT3 gravity = PhysicsSystem::Get().GetGravity();

    for (auto& cloth : m_cloths) {
        DirectX::XMFLOAT3 wind = CalculateWindAt(cloth->GetPosition(), m_simulationTime);
        cloth->StepPhysics(dt, gravity, wind, m_colliders);
        cloth->SyncToRenderer(renderer);
    }

    if (m_singleStep) {
        m_singleStep = false;
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    m_lastSimTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
}

std::shared_ptr<ClothObject> ClothSystem::GetClothById(uint32_t id) {
    for (auto& c : m_cloths) {
        if (c->GetId() == id) return c;
    }
    return nullptr;
}

std::shared_ptr<ClothObject> ClothSystem::GetSelectedCloth() {
    return GetClothById(m_selectedClothId);
}

size_t ClothSystem::GetTotalParticles() const {
    size_t total = 0;
    for (const auto& c : m_cloths) total += c->GetParticles().size();
    return total;
}

size_t ClothSystem::GetTotalConstraints() const {
    size_t total = 0;
    for (const auto& c : m_cloths) total += c->GetConstraints().size();
    return total;
}

void ClothSystem::RemoveCloth(uint32_t clothId, Renderer* renderer) {
    for (auto it = m_cloths.begin(); it != m_cloths.end(); ++it) {
        if ((*it)->GetId() == clothId) {
            if (renderer && (*it)->GetDynamicModelId() != 0) {
                renderer->RemoveDynamicModel((*it)->GetDynamicModelId());
            }
            m_cloths.erase(it);
            if (m_selectedClothId == clothId) {
                m_selectedClothId = m_cloths.empty() ? 0 : m_cloths.front()->GetId();
            }
            break;
        }
    }
}

void ClothSystem::ClearAllCloths(Renderer* renderer) {
    for (auto& cloth : m_cloths) {
        if (renderer && cloth->GetDynamicModelId() != 0) {
            renderer->RemoveDynamicModel(cloth->GetDynamicModelId());
        }
    }
    m_cloths.clear();
    m_selectedClothId = 0;
}

void ClothSystem::ResetAllCloths() {
    for (auto& cloth : m_cloths) {
        cloth->Reset();
    }
}

// -----------------------------------------------------------------------------------------
// Spawning Presets
// -----------------------------------------------------------------------------------------

std::shared_ptr<ClothObject> ClothSystem::SpawnPreset(ClothPresetType type, const DirectX::XMFLOAT3& pos, Renderer* renderer) {
    switch (type) {
        case ClothPresetType::Flag: return SpawnFlag(pos, 2.5f, 1.7f, 24, 32, renderer);
        case ClothPresetType::Curtain: return SpawnCurtain(pos, 3.0f, 2.5f, 28, 32, renderer);
        case ClothPresetType::Tablecloth: return SpawnTablecloth(pos, 2.6f, 28, renderer);
        case ClothPresetType::Cape: return SpawnCape(pos, 1.4f, 2.2f, 25, 20, renderer);
        case ClothPresetType::Trampoline: return SpawnTrampoline(pos, 3.2f, 26, renderer);
        case ClothPresetType::FreeSheet: return SpawnFreeSheet(pos, 2.5f, 2.5f, 24, 24, renderer);
        default: return SpawnFlag(pos, 2.4f, 1.6f, 24, 30, renderer);
    }
}

std::shared_ptr<ClothObject> ClothSystem::SpawnFlag(const DirectX::XMFLOAT3& pos, float width, float height, int rows, int cols, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Wind Flag");
    cloth->SetPresetType(ClothPresetType::Flag);
    cloth->ApplyMaterialPreset(ClothMaterialType::Silk);
    cloth->CreateGrid(pos, { 0.0f, 0.0f, 0.0f }, width, height, rows, cols, 3 /* LeftEdge */);

    // Add flagpole cylinder collider
    ClothCollider pole;
    pole.type = ClothColliderType::Capsule;
    pole.position = { pos.x - width * 0.5f, pos.y, pos.z };
    pole.radius = 0.04f;
    pole.height = height + 1.0f;
    AddCustomCollider(pole);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

std::shared_ptr<ClothObject> ClothSystem::SpawnCurtain(const DirectX::XMFLOAT3& pos, float width, float height, int rows, int cols, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Hanging Curtain");
    cloth->SetPresetType(ClothPresetType::Curtain);
    cloth->ApplyMaterialPreset(ClothMaterialType::Velvet);
    cloth->CreateGrid(pos, { 0.0f, 0.0f, 0.0f }, width, height, rows, cols, 1 /* TopEdge */);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

std::shared_ptr<ClothObject> ClothSystem::SpawnTablecloth(const DirectX::XMFLOAT3& pos, float size, int resolution, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Tablecloth");
    cloth->SetPresetType(ClothPresetType::Tablecloth);
    cloth->ApplyMaterialPreset(ClothMaterialType::Cotton);
    // Horizontal plane orientation: Pitch = 90 deg
    cloth->CreateGrid(pos, { 90.0f, 0.0f, 0.0f }, size, size, resolution, resolution, 0 /* None */);

    // Add a table pedestal box collider under the cloth
    ClothCollider tableCol;
    tableCol.type = ClothColliderType::Box;
    tableCol.position = { pos.x, pos.y - 0.45f, pos.z };
    tableCol.size = { size * 0.32f, 0.40f, size * 0.32f }; // Half-extents
    tableCol.friction = 0.55f;
    AddCustomCollider(tableCol);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

std::shared_ptr<ClothObject> ClothSystem::SpawnCape(const DirectX::XMFLOAT3& pos, float width, float height, int rows, int cols, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Hero Cape");
    cloth->SetPresetType(ClothPresetType::Cape);
    cloth->ApplyMaterialPreset(ClothMaterialType::Silk);
    cloth->CreateGrid(pos, { 0.0f, 0.0f, 0.0f }, width, height, rows, cols, 4 /* TwoTopCorners */);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

std::shared_ptr<ClothObject> ClothSystem::SpawnTrampoline(const DirectX::XMFLOAT3& pos, float size, int resolution, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Trampoline");
    cloth->SetPresetType(ClothPresetType::Trampoline);
    cloth->ApplyMaterialPreset(ClothMaterialType::Denim);
    cloth->GetConfig().stretchStiffness = 0.999f;
    cloth->GetConfig().damping = 0.995f;
    // Horizontal plane pinned at all 4 corners
    cloth->CreateGrid(pos, { 90.0f, 0.0f, 0.0f }, size, size, resolution, resolution, 2 /* Corners */);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

std::shared_ptr<ClothObject> ClothSystem::SpawnFreeSheet(const DirectX::XMFLOAT3& pos, float width, float height, int rows, int cols, Renderer* renderer) {
    uint32_t id = m_nextClothId++;
    auto cloth = std::make_shared<ClothObject>(id, "Free Floating Sheet");
    cloth->SetPresetType(ClothPresetType::FreeSheet);
    cloth->ApplyMaterialPreset(ClothMaterialType::Silk);
    cloth->CreateGrid(pos, { 45.0f, 30.0f, 0.0f }, width, height, rows, cols, 0 /* None */);

    if (renderer) cloth->SyncToRenderer(renderer);
    m_cloths.push_back(cloth);
    m_selectedClothId = id;
    return cloth;
}

// -----------------------------------------------------------------------------------------
// ImGui UI Drawing & Control Studio
// -----------------------------------------------------------------------------------------

void ClothSystem::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!renderer) return;

    ImGui::Begin("Cloth Physics Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "XPBD Cloth Simulation Engine | Active Objects: %zu", m_cloths.size());
    ImGui::Text("Total Particles: %zu | Constraints: %zu | Sim Step: %.3f ms",
                GetTotalParticles(), GetTotalConstraints(), m_lastSimTimeMs);

    ImGui::Separator();

    // 1. Simulation playback controls
    if (m_isPaused) {
        if (ImGui::Button(" Resume")) m_isPaused = false;
    } else {
        if (ImGui::Button(" Pause")) m_isPaused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(" Step Frame")) {
        SingleStep();
    }
    ImGui::SameLine();
    if (ImGui::Button(" Reset All Cloths")) {
        ResetAllCloths();
    }
    ImGui::SameLine();
    if (ImGui::Button(" Clear All")) {
        ClearAllCloths(renderer);
    }

    ImGui::Separator();

    // 2. Interactive Tool Selector
    ImGui::Text("Interactive Tools:");
    int toolIdx = (int)m_interactionMode;
    const char* toolNames[] = { " Grab / Drag (F / Drag)", " Pin / Unpin Brush (P)", " Knife / Scissors (X)", " Impulse Push" };
    if (ImGui::Combo("Tool Mode", &toolIdx, toolNames, IM_ARRAYSIZE(toolNames))) {
        m_interactionMode = (ClothInteractionMode)toolIdx;
    }

    ImGui::Separator();

    // 3. Spawner & Preset Creator
    if (ImGui::TreeNode("Spawn New Cloth & Presets")) {
        const char* presetNames[] = { "Custom Grid", "Wind Flag", "Hanging Curtain", "Tablecloth", "Hero Cape", "Trampoline", "Free Silk Sheet" };
        ImGui::Combo("Preset", &m_spawnPresetIdx, presetNames, IM_ARRAYSIZE(presetNames));
        ImGui::DragFloat3("Spawn Position", m_spawnPos, 0.1f, -50.0f, 50.0f, "%.2f");

        if (camera && ImGui::Button(" Set Position in Front of Camera")) {
            DirectX::XMFLOAT3 cPos = camera->GetPosition();
            DirectX::XMFLOAT3 cFwd = camera->GetForwardVector();
            m_spawnPos[0] = cPos.x + cFwd.x * 4.0f;
            m_spawnPos[1] = (std::max)(cPos.y + cFwd.y * 4.0f, 1.5f);
            m_spawnPos[2] = cPos.z + cFwd.z * 4.0f;
        }

        ImGui::SliderFloat("Width", &m_spawnWidth, 0.5f, 8.0f, "%.2f m");
        ImGui::SliderFloat("Height", &m_spawnHeight, 0.5f, 8.0f, "%.2f m");
        ImGui::SliderInt("Rows", &m_spawnRows, 4, 60);
        ImGui::SliderInt("Cols", &m_spawnCols, 4, 60);

        const char* pinModes[] = { "None", "Top Edge", "Corners", "Left Edge", "Two Top Corners", "All Perimeter" };
        ImGui::Combo("Pin Mode", &m_spawnPinMode, pinModes, IM_ARRAYSIZE(pinModes));

        if (ImGui::Button("Spawn Selected Cloth Preset")) {
            DirectX::XMFLOAT3 sp = { m_spawnPos[0], m_spawnPos[1], m_spawnPos[2] };
            if (m_spawnPresetIdx == 0) {
                uint32_t id = m_nextClothId++;
                auto cloth = std::make_shared<ClothObject>(id, "Custom Cloth");
                cloth->CreateGrid(sp, { 0.0f, 0.0f, 0.0f }, m_spawnWidth, m_spawnHeight, m_spawnRows, m_spawnCols, m_spawnPinMode);
                cloth->SyncToRenderer(renderer);
                m_cloths.push_back(cloth);
                m_selectedClothId = id;
            } else {
                SpawnPreset((ClothPresetType)m_spawnPresetIdx, sp, renderer);
            }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    // 4. Wind & Aerodynamics
    if (ImGui::TreeNode("Global Wind & Aerodynamics")) {
        ImGui::Checkbox("Enable Wind", &m_windEnabled);
        if (m_windEnabled) {
            ImGui::SliderFloat("Wind Speed", &m_windSpeed, 0.0f, 35.0f, "%.1f m/s");
            ImGui::SliderFloat("Gust Strength", &m_windGustStrength, 0.0f, 20.0f, "%.1f m/s");
            ImGui::SliderFloat("Gust Frequency", &m_windGustFrequency, 0.1f, 3.0f, "%.2f Hz");
            ImGui::SliderFloat("Turbulence", &m_windTurbulence, 0.0f, 2.0f, "%.2f");

            if (ImGui::SliderFloat("Azimuth Angle", &m_windAzimuth, 0.0f, 360.0f, "%.1f°") ||
                ImGui::SliderFloat("Elevation Angle", &m_windElevation, -45.0f, 45.0f, "%.1f°")) {
                UpdateWindFromAngles();
            }

            ImGui::Text("Wind Vector: (%.2f, %.2f, %.2f)", m_windDirection.x, m_windDirection.y, m_windDirection.z);

            if (ImGui::Button("Gentle Breeze (3 m/s)")) { m_windSpeed = 3.5f; m_windGustStrength = 2.0f; }
            ImGui::SameLine();
            if (ImGui::Button("Moderate Wind (8 m/s)")) { m_windSpeed = 8.5f; m_windGustStrength = 4.5f; }
            ImGui::SameLine();
            if (ImGui::Button("Storm Gale (18 m/s)")) { m_windSpeed = 18.0f; m_windGustStrength = 9.0f; }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    // 5. Selected Cloth Inspector & Parameters
    auto cloth = GetSelectedCloth();
    if (cloth) {
        if (ImGui::TreeNode("Selected Cloth Properties")) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Editing: %s [ID %u]", cloth->GetName().c_str(), cloth->GetId());

            // Cloth Selector Combo
            std::vector<std::string> names;
            int currentClothIdx = 0;
            for (size_t i = 0; i < m_cloths.size(); ++i) {
                names.push_back(m_cloths[i]->GetName() + " (" + std::to_string(m_cloths[i]->GetId()) + ")");
                if (m_cloths[i]->GetId() == m_selectedClothId) currentClothIdx = (int)i;
            }
            if (!names.empty() && ImGui::BeginCombo("Select Cloth", names[currentClothIdx].c_str())) {
                for (size_t i = 0; i < names.size(); ++i) {
                    bool isSelected = (currentClothIdx == (int)i);
                    if (ImGui::Selectable(names[i].c_str(), isSelected)) {
                        m_selectedClothId = m_cloths[i]->GetId();
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Material Preset Selector
            const char* matPresets[] = { "Custom", "Silk", "Cotton", "Velvet", "Denim", "Leather", "Gold Thread", "Plastic Sheet" };
            int curMat = (int)cloth->GetMaterialType();
            if (ImGui::Combo("Fabric Material", &curMat, matPresets, IM_ARRAYSIZE(matPresets))) {
                cloth->ApplyMaterialPreset((ClothMaterialType)curMat);
            }

            ImGui::Separator();
            ClothConfig& cfg = cloth->GetConfig();

            ImGui::SliderFloat("Total Mass", &cfg.totalMass, 0.1f, 20.0f, "%.2f kg");
            ImGui::SliderFloat("Stretch Stiffness", &cfg.stretchStiffness, 0.1f, 1.0f, "%.3f");
            ImGui::SliderFloat("Shear Stiffness", &cfg.shearStiffness, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Bending Stiffness", &cfg.bendStiffness, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Damping", &cfg.damping, 0.90f, 0.999f, "%.4f");
            ImGui::SliderFloat("Drag Coefficient", &cfg.dragCoefficient, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("Lift Coefficient", &cfg.liftCoefficient, 0.0f, 0.3f, "%.3f");
            ImGui::SliderFloat("Gravity Multiplier", &cfg.gravityMultiplier, -3.0f, 3.0f, "%.2f");
            ImGui::SliderInt("Sub-steps", &cfg.subSteps, 1, 8);
            ImGui::SliderInt("Solver Iterations", &cfg.solverIterations, 1, 30);
            ImGui::SliderFloat("Collision Thickness", &cfg.collisionThickness, 0.005f, 0.15f, "%.3f m");
            ImGui::SliderFloat("Surface Friction", &cfg.surfaceFriction, 0.0f, 1.0f, "%.2f");

            ImGui::Checkbox("Self-Collision", &cfg.enableSelfCollision);
            if (cfg.enableSelfCollision) {
                ImGui::SliderFloat("Self Collision Radius", &cfg.selfCollisionRadius, 0.01f, 0.15f, "%.3f m");
            }

            ImGui::Checkbox("Enable Tearing", &cfg.enableTearing);
            if (cfg.enableTearing) {
                ImGui::SliderFloat("Tear Strain Threshold", &cfg.tearThreshold, 1.1f, 3.0f, "%.2fx");
            }

            ImGui::Separator();
            ImGui::Text("Pinning Management:");
            if (ImGui::Button(" Pin Top")) cloth->PinTopEdge();
            ImGui::SameLine();
            if (ImGui::Button(" Pin Left")) cloth->PinLeftEdge();
            ImGui::SameLine();
            if (ImGui::Button(" Pin Corners")) cloth->PinFourCorners();
            ImGui::SameLine();
            if (ImGui::Button(" Unpin All")) cloth->UnpinAll();

            ImGui::Separator();
            ImGui::Text("PBR Material Rendering:");
            Renderer::PBRMaterial& mat = cloth->GetMaterial();
            float alb[3] = { mat.albedo.x, mat.albedo.y, mat.albedo.z };
            if (ImGui::ColorEdit3("Albedo Color", alb)) {
                mat.albedo = { alb[0], alb[1], alb[2] };
            }
            ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Reflectance", &mat.reflectance, 0.0f, 1.0f);
            ImGui::SliderFloat2("UV Tiling", &mat.uvTiling.x, 0.1f, 10.0f);

            ImGui::Separator();
            if (ImGui::Button(" Reset This Cloth")) cloth->Reset();
            ImGui::SameLine();
            if (ImGui::Button(" Delete Cloth")) {
                RemoveCloth(cloth->GetId(), renderer);
            }

            ImGui::TreePop();
        }
    }

    ImGui::End();
#endif
}

} // namespace EnginePhysics
