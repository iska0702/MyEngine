#include "HairSystem.h"
#include "Core/Window.h"
#include "Renderer/Camera.h"
#include "Physics/PhysicsSystem.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace EnginePhysics {

// Helper math
inline DirectX::XMFLOAT3 HVecAdd(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline DirectX::XMFLOAT3 HVecSub(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline DirectX::XMFLOAT3 HVecMul(const DirectX::XMFLOAT3& a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
inline float HVecDot(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline DirectX::XMFLOAT3 HVecCross(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
inline float HVecLengthSq(const DirectX::XMFLOAT3& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}
inline float HVecLength(const DirectX::XMFLOAT3& v) {
    return std::sqrt(HVecLengthSq(v));
}
inline DirectX::XMFLOAT3 HVecNormalize(const DirectX::XMFLOAT3& v) {
    float len = HVecLength(v);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }
    return { 0.0f, 1.0f, 0.0f };
}

// Catmull-Rom cubic spline interpolation
static DirectX::XMFLOAT3 CatmullRom(const DirectX::XMFLOAT3& p0, const DirectX::XMFLOAT3& p1,
                                    const DirectX::XMFLOAT3& p2, const DirectX::XMFLOAT3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    float f0 = -0.5f * t3 + t2 - 0.5f * t;
    float f1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
    float f2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float f3 =  0.5f * t3 - 0.5f * t2;

    return {
        p0.x * f0 + p1.x * f1 + p2.x * f2 + p3.x * f3,
        p0.y * f0 + p1.y * f1 + p2.y * f2 + p3.y * f3,
        p0.z * f0 + p1.z * f1 + p2.z * f2 + p3.z * f3
    };
}

// Distance from point to line segment
static float HDistPointToSegmentSq(const DirectX::XMFLOAT3& p, const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, DirectX::XMFLOAT3& closestPt) {
    DirectX::XMFLOAT3 ab = HVecSub(b, a);
    DirectX::XMFLOAT3 ap = HVecSub(p, a);
    float abLenSq = HVecLengthSq(ab);
    if (abLenSq < 1e-6f) {
        closestPt = a;
        return HVecLengthSq(ap);
    }
    float t = (std::clamp)(HVecDot(ap, ab) / abLenSq, 0.0f, 1.0f);
    closestPt = HVecAdd(a, HVecMul(ab, t));
    return HVecLengthSq(HVecSub(p, closestPt));
}

// =========================================================================================
// HairObject Implementation
// =========================================================================================

HairObject::HairObject(uint32_t id, const std::string& name)
    : m_id(id), m_name(name) {
    ApplyColorPreset(0); // Default: Brunette
    m_material.roughness = m_config.roughness;
    m_material.metallic = m_config.metallic;
    m_material.reflectance = 0.60f;
}

HairObject::~HairObject() {
}

void HairObject::ApplyColorPreset(int presetIdx) {
    switch (presetIdx) {
        case 0: // Brunette (Rich Chestnut & Caramel)
            m_config.rootColor = { 0.12f, 0.07f, 0.03f };
            m_config.tipColor  = { 0.65f, 0.42f, 0.18f };
            break;
        case 1: // Golden Honey Blonde
            m_config.rootColor = { 0.45f, 0.30f, 0.12f };
            m_config.tipColor  = { 0.95f, 0.82f, 0.48f };
            break;
        case 2: // Raven Glossy Black
            m_config.rootColor = { 0.04f, 0.04f, 0.05f };
            m_config.tipColor  = { 0.18f, 0.18f, 0.22f };
            break;
        case 3: // Crimson Burgundy Red
            m_config.rootColor = { 0.30f, 0.05f, 0.08f };
            m_config.tipColor  = { 0.85f, 0.15f, 0.22f };
            break;
        case 4: // Platinum Silver
            m_config.rootColor = { 0.40f, 0.42f, 0.45f };
            m_config.tipColor  = { 0.92f, 0.94f, 0.98f };
            break;
        case 5: // Pastel Rose Pink
            m_config.rootColor = { 0.55f, 0.15f, 0.25f };
            m_config.tipColor  = { 0.98f, 0.60f, 0.75f };
            break;
        case 6: // Cyan / Turquoise
            m_config.rootColor = { 0.05f, 0.25f, 0.30f };
            m_config.tipColor  = { 0.20f, 0.85f, 0.80f };
            break;
        default:
            break;
    }
    m_material.albedo = m_config.tipColor;
}

void HairObject::GenerateHairstyle(HairHairstyleType type) {
    m_hairstyleType = type;
    m_strands.clear();
    m_renderVertices.clear();
    m_indices.clear();

    int strandCount = (std::clamp)(m_config.numStrands, 20, 600);
    int nodesCount  = (std::clamp)(m_config.nodesPerStrand, 6, 24);
    float totalLength = m_config.hairLength;
    float segLength   = totalLength / (float)(nodesCount - 1);

    m_strands.resize(strandCount);

    using namespace DirectX;
    XMMATRIX headRotMat = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(m_headRotation.x),
        XMConvertToRadians(m_headRotation.y),
        XMConvertToRadians(m_headRotation.z)
    );

    // Golden ratio spherical spiral / Fibonacci lattice for scalp follicle distribution
    float goldenRatio = (1.0f + std::sqrt(5.0f)) * 0.5f;

    for (int s = 0; s < strandCount; ++s) {
        HairStrand& strand = m_strands[s];
        strand.nodes.resize(nodesCount);
        strand.activeNodeCount = nodesCount;
        strand.isCut = false;
        strand.curlPhase = (float)s * 0.45f;
        strand.wavinessPhase = (float)s * 0.65f;

        // Scalp follicle placement based on hairstyle preset
        float u = (float)s / (float)strandCount;
        float theta = 2.0f * XM_PI * u * (float)strandCount / goldenRatio;

        float phi = 0.0f;
        float scalpY = 0.0f;
        float scalpX = 0.0f;
        float scalpZ = 0.0f;
        float lengthMult = 1.0f;

        switch (type) {
            case HairHairstyleType::LongFlowing: {
                // Upper & back hemisphere of skull
                float t = (float)s / (float)(strandCount - 1);
                phi = std::acos(1.0f - t * 1.55f); // Focus on upper and back/sides
                if (phi > XM_PIDIV2 + 0.35f) phi = XM_PIDIV2 + 0.35f;

                scalpX = std::sin(phi) * std::sin(theta) * m_config.headRadiusX;
                scalpY = std::cos(phi) * m_config.headRadiusY;
                scalpZ = std::sin(phi) * std::cos(theta) * m_config.headRadiusZ;

                // Bangs in front are shorter, back is longer
                if (scalpZ > 0.15f && scalpY > 0.0f) {
                    lengthMult = 0.45f + 0.35f * (1.0f - (scalpZ / m_config.headRadiusZ)); // Bangs
                } else {
                    lengthMult = 0.90f + 0.20f * (float)(s % 5) / 5.0f; // Long flowing back
                }
                break;
            }
            case HairHairstyleType::Ponytail: {
                // Converge scalp hairs toward tie point at (0, 0.25, -0.32)
                float t = (float)s / (float)(strandCount - 1);
                phi = std::acos(1.0f - t * 1.6f);

                scalpX = std::sin(phi) * std::sin(theta) * m_config.headRadiusX;
                scalpY = std::cos(phi) * m_config.headRadiusY;
                scalpZ = std::sin(phi) * std::cos(theta) * m_config.headRadiusZ;

                // 80% of strands are in the long swinging ponytail bundle
                if (s > strandCount / 5) {
                    scalpX = 0.0f + ((float)(s % 7) - 3.0f) * 0.015f;
                    scalpY = 0.22f + ((float)(s % 5) - 2.0f) * 0.015f;
                    scalpZ = -m_config.headRadiusZ * 0.85f;
                    lengthMult = 1.15f;
                } else {
                    lengthMult = 0.35f; // Scalp base strands
                }
                break;
            }
            case HairHairstyleType::BobCut: {
                float t = (float)s / (float)(strandCount - 1);
                phi = std::acos(1.0f - t * 1.5f);

                scalpX = std::sin(phi) * std::sin(theta) * m_config.headRadiusX;
                scalpY = std::cos(phi) * m_config.headRadiusY;
                scalpZ = std::sin(phi) * std::cos(theta) * m_config.headRadiusZ;

                lengthMult = 0.55f + 0.15f * std::cos(theta); // Even bob cut
                break;
            }
            case HairHairstyleType::TwinTails: {
                // Two bunches on left & right
                bool isRight = (s % 2 == 0);
                float sign = isRight ? 1.0f : -1.0f;
                float bunchRadius = 0.04f;
                float bunAngle = (float)s * 0.8f;

                scalpX = sign * (m_config.headRadiusX * 0.85f + std::sin(bunAngle) * bunchRadius);
                scalpY = 0.15f + std::cos(bunAngle) * bunchRadius;
                scalpZ = -0.05f + std::sin(bunAngle * 1.5f) * 0.02f;
                lengthMult = 1.0f + 0.15f * (float)(s % 4) / 4.0f;
                break;
            }
            case HairHairstyleType::CurlyWavy: {
                float t = (float)s / (float)(strandCount - 1);
                phi = std::acos(1.0f - t * 1.5f);

                scalpX = std::sin(phi) * std::sin(theta) * m_config.headRadiusX;
                scalpY = std::cos(phi) * m_config.headRadiusY;
                scalpZ = std::sin(phi) * std::cos(theta) * m_config.headRadiusZ;
                lengthMult = 0.95f;
                break;
            }
            case HairHairstyleType::FurBall: {
                // Uniform full spherical distribution
                float t = (float)s / (float)(strandCount - 1);
                phi = std::acos(1.0f - 2.0f * t);

                scalpX = std::sin(phi) * std::sin(theta) * m_config.headRadiusX;
                scalpY = std::cos(phi) * m_config.headRadiusY;
                scalpZ = std::sin(phi) * std::cos(theta) * m_config.headRadiusZ;
                lengthMult = 0.40f;
                break;
            }
            default:
                scalpX = std::sin(theta) * m_config.headRadiusX;
                scalpY = m_config.headRadiusY * 0.8f;
                scalpZ = std::cos(theta) * m_config.headRadiusZ;
                lengthMult = 1.0f;
                break;
        }

        strand.scalpLocalPos = { scalpX, scalpY, scalpZ };
        strand.scalpLocalNormal = HVecNormalize({ scalpX / (m_config.headRadiusX + 0.01f),
                                                 scalpY / (m_config.headRadiusY + 0.01f),
                                                 scalpZ / (m_config.headRadiusZ + 0.01f) });
        strand.lengthMultiplier = lengthMult;

        // Build strand nodes along rest curve
        DirectX::XMFLOAT3 curLocalPos = strand.scalpLocalPos;
        float actualSegLen = segLength * lengthMult;

        for (int n = 0; n < nodesCount; ++n) {
            HairNode& node = strand.nodes[n];
            node.restLength = actualSegLen;
            node.invMass = (n == 0) ? 0.0f : 1.0f;
            node.isGrabbed = false;

            if (n == 0) {
                node.restLocalPos = curLocalPos;
            } else {
                // Growth direction: combination of scalp normal, gravity downward bias, and preset shape
                float tNode = (float)n / (float)(nodesCount - 1);
                DirectX::XMFLOAT3 dir;

                if (type == HairHairstyleType::FurBall) {
                    dir = strand.scalpLocalNormal; // Radial outward
                } else if (type == HairHairstyleType::Ponytail && s > strandCount / 5) {
                    // Ponytail flows backward and down
                    dir = { ((float)(s % 3) - 1.0f) * 0.05f, -0.85f, -0.45f };
                } else {
                    // Flow over skull down to shoulders
                    dir = {
                        strand.scalpLocalNormal.x * 0.45f,
                        -0.85f - tNode * 0.35f,
                        strand.scalpLocalNormal.z * 0.45f
                    };
                }
                dir = HVecNormalize(dir);

                // Add curly / wavy rest offsets
                if (type == HairHairstyleType::CurlyWavy) {
                    float wave = std::sin(tNode * m_config.wavinessFreq * 2.0f * XM_PI + strand.wavinessPhase);
                    float curl = std::cos(tNode * m_config.wavinessFreq * 2.0f * XM_PI + strand.curlPhase);
                    dir.x += wave * m_config.wavinessAmp * 2.0f;
                    dir.z += curl * m_config.curlRadius * 2.0f;
                    dir = HVecNormalize(dir);
                }

                curLocalPos = HVecAdd(curLocalPos, HVecMul(dir, actualSegLen));
                node.restLocalPos = curLocalPos;
                node.restDirection = dir;
            }

            // Transform restLocalPos into initial world space
            XMVECTOR localVec = XMVectorSet(node.restLocalPos.x, node.restLocalPos.y, node.restLocalPos.z, 1.0f);
            XMVECTOR worldVec = XMVector3Transform(localVec, headRotMat);
            XMFLOAT3 worldPosF;
            XMStoreFloat3(&worldPosF, worldVec);

            node.position = HVecAdd(m_headPosition, worldPosF);
            node.prevPosition = node.position;
            node.predictedPosition = node.position;
            node.velocity = { 0.0f, 0.0f, 0.0f };
            node.tangent = { 0.0f, -1.0f, 0.0f };
            node.normal = { 0.0f, 0.0f, 1.0f };
        }
    }

    m_prevHeadPosition = m_headPosition;
    m_prevHeadRotation = m_headRotation;
    m_topologyDirty = true;
}

void HairObject::Reset() {
    using namespace DirectX;
    XMMATRIX headRotMat = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(m_headRotation.x),
        XMConvertToRadians(m_headRotation.y),
        XMConvertToRadians(m_headRotation.z)
    );

    for (auto& strand : m_strands) {
        strand.activeNodeCount = (int)strand.nodes.size();
        strand.isCut = false;
        for (auto& node : strand.nodes) {
            XMVECTOR localVec = XMVectorSet(node.restLocalPos.x, node.restLocalPos.y, node.restLocalPos.z, 1.0f);
            XMVECTOR worldVec = XMVector3Transform(localVec, headRotMat);
            XMFLOAT3 worldPosF;
            XMStoreFloat3(&worldPosF, worldVec);

            node.position = HVecAdd(m_headPosition, worldPosF);
            node.prevPosition = node.position;
            node.predictedPosition = node.position;
            node.velocity = { 0.0f, 0.0f, 0.0f };
            node.isGrabbed = false;
        }
    }
    m_topologyDirty = true;
}

// -----------------------------------------------------------------------------------------
// Physics Step & Constraint Solvers
// -----------------------------------------------------------------------------------------

void HairObject::SolveCollisions(HairNode& node, const std::vector<HairCollider>& externalColliders) {
    if (node.invMass <= 0.0f || node.isGrabbed) return;

    // 1. Head Ellipsoid Collision
    // Local coordinates relative to head center
    DirectX::XMFLOAT3 rel = HVecSub(node.predictedPosition, m_headPosition);
    float rx = m_config.headRadiusX;
    float ry = m_config.headRadiusY;
    float rz = m_config.headRadiusZ;

    float ellipVal = (rel.x * rel.x) / (rx * rx) +
                     (rel.y * rel.y) / (ry * ry) +
                     (rel.z * rel.z) / (rz * rz);

    if (ellipVal < 1.0f && ellipVal > 1e-5f) {
        // Compute outward ellipsoid gradient normal
        DirectX::XMFLOAT3 grad = {
            rel.x / (rx * rx),
            rel.y / (ry * ry),
            rel.z / (rz * rz)
        };
        DirectX::XMFLOAT3 norm = HVecNormalize(grad);
        float scale = 1.0f / std::sqrt(ellipVal);
        DirectX::XMFLOAT3 surfacePt = { rel.x * scale, rel.y * scale, rel.z * scale };
        node.predictedPosition = HVecAdd(m_headPosition, HVecAdd(surfacePt, HVecMul(norm, 0.015f)));
    }

    // 2. Neck Capsule Collision
    DirectX::XMFLOAT3 neckBase = { m_headPosition.x, m_headPosition.y - m_config.headRadiusY - m_config.neckLength, m_headPosition.z };
    DirectX::XMFLOAT3 neckTop  = { m_headPosition.x, m_headPosition.y - m_config.headRadiusY * 0.4f, m_headPosition.z };
    DirectX::XMFLOAT3 closestNeckPt;
    float neckDistSq = HDistPointToSegmentSq(node.predictedPosition, neckBase, neckTop, closestNeckPt);
    float targetNeckR = m_config.neckRadius + 0.015f;
    if (neckDistSq < targetNeckR * targetNeckR) {
        float dist = std::sqrt(neckDistSq);
        DirectX::XMFLOAT3 norm = (dist > 1e-5f) ? HVecMul(HVecSub(node.predictedPosition, closestNeckPt), 1.0f / dist) : DirectX::XMFLOAT3(0, 0, 1);
        node.predictedPosition = HVecAdd(closestNeckPt, HVecMul(norm, targetNeckR));
    }

    // 3. Shoulders Capsule Collision (Horizontal bar across chest/back)
    float shoulderY = neckBase.y;
    float halfShoulderW = m_config.shoulderWidth * 0.5f;
    DirectX::XMFLOAT3 shL = { m_headPosition.x - halfShoulderW, shoulderY, m_headPosition.z };
    DirectX::XMFLOAT3 shR = { m_headPosition.x + halfShoulderW, shoulderY, m_headPosition.z };
    DirectX::XMFLOAT3 closestShPt;
    float shDistSq = HDistPointToSegmentSq(node.predictedPosition, shL, shR, closestShPt);
    float targetShR = m_config.shoulderRadius + 0.02f;
    if (shDistSq < targetShR * targetShR) {
        float dist = std::sqrt(shDistSq);
        DirectX::XMFLOAT3 norm = (dist > 1e-5f) ? HVecMul(HVecSub(node.predictedPosition, closestShPt), 1.0f / dist) : DirectX::XMFLOAT3(0, 1, 0);
        node.predictedPosition = HVecAdd(closestShPt, HVecMul(norm, targetShR));
    }

    // 4. Floor Plane Collision (y = 0.0)
    if (node.predictedPosition.y < 0.02f) {
        node.predictedPosition.y = 0.02f;
    }

    // 5. External Colliders (Scene Spheres, Boxes, etc.)
    for (const auto& col : externalColliders) {
        if (col.type == HairColliderType::Sphere) {
            DirectX::XMFLOAT3 diff = HVecSub(node.predictedPosition, col.position);
            float d = HVecLength(diff);
            float targetD = col.radius + 0.02f;
            if (d < targetD) {
                DirectX::XMFLOAT3 n = (d > 1e-5f) ? HVecMul(diff, 1.0f / d) : DirectX::XMFLOAT3(0, 1, 0);
                node.predictedPosition = HVecAdd(col.position, HVecMul(n, targetD));
            }
        }
    }
}

void HairObject::SolveStrandConstraints(HairStrand& strand, const DirectX::XMMATRIX& headTransform) {
    using namespace DirectX;
    int count = strand.activeNodeCount;
    if (count < 2) return;

    // 1. Distance / Stretch Constraints (Forward & Backward Gauss-Seidel)
    for (int i = 1; i < count; ++i) {
        HairNode& p0 = strand.nodes[i - 1];
        HairNode& p1 = strand.nodes[i];

        DirectX::XMFLOAT3 delta = HVecSub(p1.predictedPosition, p0.predictedPosition);
        float dist = HVecLength(delta);
        if (dist < 1e-6f) continue;

        float error = (dist - p1.restLength);
        float w0 = p0.invMass;
        float w1 = p1.invMass;
        float wSum = w0 + w1;
        if (wSum < 1e-7f) continue;

        DirectX::XMFLOAT3 dir = HVecMul(delta, 1.0f / dist);
        float correction = (error / wSum) * m_config.stretchStiffness;

        if (w0 > 0.0f && !p0.isGrabbed) p0.predictedPosition = HVecAdd(p0.predictedPosition, HVecMul(dir, w0 * correction));
        if (w1 > 0.0f && !p1.isGrabbed) p1.predictedPosition = HVecSub(p1.predictedPosition, HVecMul(dir, w1 * correction));
    }

    // 2. Shape Retention & Bending Constraints (Cosserat Rod rest pose preservation)
    float shapeWeight = m_config.shapeRetention;
    float bendWeight  = m_config.bendStiffness;

    if (shapeWeight > 0.001f || bendWeight > 0.001f) {
        for (int i = 1; i < count; ++i) {
            HairNode& node = strand.nodes[i];
            if (node.invMass <= 0.0f || node.isGrabbed) continue;

            // Target rest position in world space based on head transform
            XMVECTOR localVec = XMVectorSet(node.restLocalPos.x, node.restLocalPos.y, node.restLocalPos.z, 1.0f);
            XMVECTOR worldVec = XMVector3Transform(localVec, headTransform);
            XMFLOAT3 targetWorldPos;
            XMStoreFloat3(&targetWorldPos, worldVec);
            targetWorldPos = HVecAdd(m_headPosition, targetWorldPos);

            // Shape matching blending
            DirectX::XMFLOAT3 shapeDiff = HVecSub(targetWorldPos, node.predictedPosition);
            node.predictedPosition = HVecAdd(node.predictedPosition, HVecMul(shapeDiff, shapeWeight * 0.12f));

            // Angular bending constraint towards parent's expected direction
            if (i >= 2) {
                HairNode& prev = strand.nodes[i - 1];
                HairNode& prev2 = strand.nodes[i - 2];
                DirectX::XMFLOAT3 prevDir = HVecNormalize(HVecSub(prev.predictedPosition, prev2.predictedPosition));
                DirectX::XMFLOAT3 expectedPos = HVecAdd(prev.predictedPosition, HVecMul(prevDir, node.restLength));
                DirectX::XMFLOAT3 bendDiff = HVecSub(expectedPos, node.predictedPosition);
                node.predictedPosition = HVecAdd(node.predictedPosition, HVecMul(bendDiff, bendWeight * 0.08f));
            }
        }
    }
}

void HairObject::StepPhysics(float dt, const DirectX::XMFLOAT3& gravity, const DirectX::XMFLOAT3& windVec,
                            const std::vector<HairCollider>& externalColliders) {
    if (m_strands.empty()) return;

    int subSteps = (std::clamp)(m_config.subSteps, 1, 8);
    float dtSub = dt / (float)subSteps;
    DirectX::XMFLOAT3 effGravity = HVecMul(gravity, m_config.gravityMultiplier);

    using namespace DirectX;
    XMMATRIX headRotMat = XMMatrixRotationRollPitchYaw(
        XMConvertToRadians(m_headRotation.x),
        XMConvertToRadians(m_headRotation.y),
        XMConvertToRadians(m_headRotation.z)
    );

    // Compute head linear & angular velocity for dynamic inertia
    DirectX::XMFLOAT3 headVelocity = HVecMul(HVecSub(m_headPosition, m_prevHeadPosition), 1.0f / (std::max)(dt, 1e-4f));

    for (int step = 0; step < subSteps; ++step) {
        // 1. Root update & Symplectic Euler Predictor
        for (auto& strand : m_strands) {
            int count = strand.activeNodeCount;
            if (count == 0) continue;

            // Anchor follicle root node (node 0) rigidly to scalp
            XMVECTOR localRoot = XMVectorSet(strand.scalpLocalPos.x, strand.scalpLocalPos.y, strand.scalpLocalPos.z, 1.0f);
            XMVECTOR worldRoot = XMVector3Transform(localRoot, headRotMat);
            XMFLOAT3 rootWorldF;
            XMStoreFloat3(&rootWorldF, worldRoot);

            HairNode& root = strand.nodes[0];
            root.position = HVecAdd(m_headPosition, rootWorldF);
            root.predictedPosition = root.position;
            root.velocity = headVelocity;

            for (int i = 1; i < count; ++i) {
                HairNode& node = strand.nodes[i];
                if (node.isGrabbed) continue;

                // Velocity damping
                node.velocity = HVecMul(node.velocity, m_config.damping);

                // Gravity & Acceleration
                node.velocity = HVecAdd(node.velocity, HVecMul(effGravity, dtSub));

                // Aerodynamic Air Drag & Wind
                if (m_config.enableWind) {
                    DirectX::XMFLOAT3 vRel = HVecSub(windVec, node.velocity);
                    float vRelLen = HVecLength(vRel);
                    if (vRelLen > 1e-4f) {
                        float dragMag = 0.5f * 1.225f * m_config.airDrag * vRelLen;
                        DirectX::XMFLOAT3 fAero = HVecMul(vRel, dragMag * dtSub);
                        node.velocity = HVecAdd(node.velocity, fAero);
                    }
                }

                // Predict position
                node.predictedPosition = HVecAdd(node.position, HVecMul(node.velocity, dtSub));
            }
        }

        // 2. XPBD Constraint Solver Iterations
        int iters = (std::clamp)(m_config.solverIterations, 1, 25);
        for (int iter = 0; iter < iters; ++iter) {
            for (auto& strand : m_strands) {
                SolveStrandConstraints(strand, headRotMat);
                for (int i = 1; i < strand.activeNodeCount; ++i) {
                    SolveCollisions(strand.nodes[i], externalColliders);
                }
            }
        }

        // 3. Update Velocities and Final Positions
        for (auto& strand : m_strands) {
            for (int i = 1; i < strand.activeNodeCount; ++i) {
                HairNode& node = strand.nodes[i];
                if (!node.isGrabbed) {
                    node.velocity = HVecMul(HVecSub(node.predictedPosition, node.position), 1.0f / dtSub);
                    node.prevPosition = node.position;
                    node.position = node.predictedPosition;
                }
            }
        }
    }

    m_prevHeadPosition = m_headPosition;
    m_prevHeadRotation = m_headRotation;
}

// -----------------------------------------------------------------------------------------
// Ribbon Tessellation & DirectX 12 Mesh Synchronization
// -----------------------------------------------------------------------------------------

void HairObject::TessellateRibbons(const DirectX::XMFLOAT3& cameraPos) {
    m_renderVertices.clear();
    m_indices.clear();

    float rootW = m_config.rootWidth;
    float tipW  = m_config.tipWidth;

    for (auto& strand : m_strands) {
        int count = strand.activeNodeCount;
        if (count < 2) continue;

        // Calculate smooth tangents for each node
        for (int i = 0; i < count; ++i) {
            DirectX::XMFLOAT3 tan;
            if (i == 0) {
                tan = HVecSub(strand.nodes[1].position, strand.nodes[0].position);
            } else if (i == count - 1) {
                tan = HVecSub(strand.nodes[count - 1].position, strand.nodes[count - 2].position);
            } else {
                tan = HVecSub(strand.nodes[i + 1].position, strand.nodes[i - 1].position);
            }
            strand.nodes[i].tangent = HVecNormalize(tan);
        }

        // Generate Catmull-Rom interpolated ribbon points (2 interpolated points per segment)
        std::vector<DirectX::XMFLOAT3> splinePts;
        std::vector<DirectX::XMFLOAT3> splineTans;
        std::vector<float> splineUs; // [0..1] root to tip

        int subDivs = 2;
        for (int i = 0; i < count - 1; ++i) {
            const DirectX::XMFLOAT3& p0 = (i > 0) ? strand.nodes[i - 1].position : strand.nodes[i].position;
            const DirectX::XMFLOAT3& p1 = strand.nodes[i].position;
            const DirectX::XMFLOAT3& p2 = strand.nodes[i + 1].position;
            const DirectX::XMFLOAT3& p3 = (i + 2 < count) ? strand.nodes[i + 2].position : p2;

            for (int s = 0; s < subDivs; ++s) {
                float t = (float)s / (float)subDivs;
                DirectX::XMFLOAT3 pos = CatmullRom(p0, p1, p2, p3, t);
                DirectX::XMFLOAT3 nextPos = CatmullRom(p0, p1, p2, p3, t + 0.05f);
                DirectX::XMFLOAT3 tan = HVecNormalize(HVecSub(nextPos, pos));
                float u = ((float)i + t) / (float)(count - 1);

                splinePts.push_back(pos);
                splineTans.push_back(tan);
                splineUs.push_back(u);
            }
        }
        // Add final tip point
        splinePts.push_back(strand.nodes[count - 1].position);
        splineTans.push_back(strand.nodes[count - 1].tangent);
        splineUs.push_back(1.0f);

        // Build Camera-Facing Ribbons
        uint32_t baseVertexIdx = (uint32_t)m_renderVertices.size();
        size_t ptCount = splinePts.size();

        for (size_t k = 0; k < ptCount; ++k) {
            float u = splineUs[k];
            float width = rootW * (1.0f - u) + tipW * u;
            width = (std::max)(width, 0.002f);

            // Vector towards camera with safe fallback when camera gets extremely close
            DirectX::XMFLOAT3 toCamVec = HVecSub(cameraPos, splinePts[k]);
            float camDistSq = HVecLengthSq(toCamVec);
            DirectX::XMFLOAT3 toCam;
            if (camDistSq > 0.0004f && !std::isnan(camDistSq) && !std::isinf(camDistSq)) { // > 2cm distance
                toCam = HVecMul(toCamVec, 1.0f / std::sqrt(camDistSq));
            } else {
                toCam = { 0.0f, 0.0f, 1.0f }; // Safe fallback when camera is inside the strand
            }

            // Ribbon perpendicular vector facing camera with collinearity guard
            DirectX::XMFLOAT3 right = HVecCross(splineTans[k], toCam);
            float rightLenSq = HVecLengthSq(right);
            if (rightLenSq < 1e-4f || std::isnan(rightLenSq)) {
                DirectX::XMFLOAT3 altAxis = (std::abs(splineTans[k].y) > 0.85f) ? DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f) : DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
                right = HVecCross(splineTans[k], altAxis);
                rightLenSq = HVecLengthSq(right);
            }
            if (rightLenSq > 1e-6f && !std::isnan(rightLenSq)) {
                right = HVecMul(right, 1.0f / std::sqrt(rightLenSq));
            } else {
                right = { 1.0f, 0.0f, 0.0f };
            }

            DirectX::XMFLOAT3 norm = HVecCross(right, splineTans[k]);
            float normLenSq = HVecLengthSq(norm);
            if (normLenSq > 1e-6f && !std::isnan(normLenSq)) {
                norm = HVecMul(norm, 1.0f / std::sqrt(normLenSq));
            } else {
                norm = { 0.0f, 0.0f, 1.0f };
            }

            DirectX::XMFLOAT3 posL = HVecSub(splinePts[k], HVecMul(right, width * 0.5f));
            DirectX::XMFLOAT3 posR = HVecAdd(splinePts[k], HVecMul(right, width * 0.5f));

            if (std::isnan(posL.x) || std::isinf(posL.x)) posL = splinePts[k];
            if (std::isnan(posR.x) || std::isinf(posR.x)) posR = splinePts[k];

            // Vertex Color stores root-to-tip gradient factor in R,G and UV in Z,W
            DirectX::XMFLOAT3 color = {
                m_config.rootColor.x * (1.0f - u) + m_config.tipColor.x * u,
                m_config.rootColor.y * (1.0f - u) + m_config.tipColor.y * u,
                m_config.rootColor.z * (1.0f - u) + m_config.tipColor.z * u
            };

            Renderer::Vertex vL;
            vL.position = posL;
            vL.normal = norm;
            vL.color = { color.x, color.y, 0.0f, u };
            m_renderVertices.push_back(vL);

            Renderer::Vertex vR;
            vR.position = posR;
            vR.normal = norm;
            vR.color = { color.x, color.y, 1.0f, u };
            m_renderVertices.push_back(vR);
        }

        // Build Triangles for Ribbon Strip
        for (size_t k = 0; k < ptCount - 1; ++k) {
            uint32_t i0 = (uint32_t)(baseVertexIdx + k * 2 + 0);
            uint32_t i1 = (uint32_t)(baseVertexIdx + k * 2 + 1);
            uint32_t i2 = (uint32_t)(baseVertexIdx + (k + 1) * 2 + 1);
            uint32_t i3 = (uint32_t)(baseVertexIdx + (k + 1) * 2 + 0);

            // Triangle 1: i0 -> i1 -> i2
            m_indices.push_back(i0);
            m_indices.push_back(i1);
            m_indices.push_back(i2);

            // Triangle 2: i0 -> i2 -> i3
            m_indices.push_back(i0);
            m_indices.push_back(i2);
            m_indices.push_back(i3);
        }
    }
}

void HairObject::SyncToRenderer(Renderer* renderer) {
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

// -----------------------------------------------------------------------------------------
// Interactive Tools: Raycasting, Cutting, Brushing
// -----------------------------------------------------------------------------------------

bool HairObject::Raycast(const DirectX::XMFLOAT3& rayOrigin, const DirectX::XMFLOAT3& rayDir,
                         float& outDist, uint32_t& outStrandIdx, uint32_t& outNodeIdx, DirectX::XMFLOAT3& outHitPoint) {
    float bestDist = 1e9f;
    bool hasHit = false;

    for (size_t s = 0; s < m_strands.size(); ++s) {
        const auto& strand = m_strands[s];
        for (int i = 0; i < strand.activeNodeCount; ++i) {
            DirectX::XMFLOAT3 closestPt;
            float dSq = HDistPointToSegmentSq(strand.nodes[i].position, rayOrigin, HVecAdd(rayOrigin, HVecMul(rayDir, 40.0f)), closestPt);
            if (dSq < 0.06f * 0.06f) {
                float dist = HVecLength(HVecSub(closestPt, rayOrigin));
                if (dist < bestDist) {
                    bestDist = dist;
                    outDist = dist;
                    outStrandIdx = (uint32_t)s;
                    outNodeIdx = (uint32_t)i;
                    outHitPoint = strand.nodes[i].position;
                    hasHit = true;
                }
            }
        }
    }
    return hasHit;
}

void HairObject::CutStrands(const DirectX::XMFLOAT3& rayStart, const DirectX::XMFLOAT3& rayDir, float maxDist) {
    bool cutAny = false;
    DirectX::XMFLOAT3 rayEnd = HVecAdd(rayStart, HVecMul(rayDir, maxDist));

    for (auto& strand : m_strands) {
        for (int i = 2; i < strand.activeNodeCount; ++i) {
            DirectX::XMFLOAT3 closestPt;
            float dSq = HDistPointToSegmentSq(strand.nodes[i].position, rayStart, rayEnd, closestPt);
            if (dSq < 0.05f * 0.05f) {
                strand.activeNodeCount = i;
                strand.isCut = true;
                cutAny = true;
                break;
            }
        }
    }
    if (cutAny) {
        m_topologyDirty = true;
    }
}

void HairObject::BrushHair(const DirectX::XMFLOAT3& brushOrigin, const DirectX::XMFLOAT3& brushDir, float radius, float strength) {
    float rSq = radius * radius;
    for (auto& strand : m_strands) {
        for (int i = 1; i < strand.activeNodeCount; ++i) {
            DirectX::XMFLOAT3 diff = HVecSub(strand.nodes[i].position, brushOrigin);
            float dSq = HVecLengthSq(diff);
            if (dSq < rSq) {
                float factor = (1.0f - std::sqrt(dSq) / radius) * strength;
                strand.nodes[i].velocity = HVecAdd(strand.nodes[i].velocity, HVecMul(brushDir, factor));
            }
        }
    }
}

// =========================================================================================
// HairSystem Implementation
// =========================================================================================

HairSystem& HairSystem::Get() {
    static HairSystem s_instance;
    return s_instance;
}

HairSystem::HairSystem() {
}

HairSystem::~HairSystem() {
}

bool HairSystem::Initialize(Renderer* renderer) {
    LOG("HairSystem::Initialize starting...");
    LOG("HairSystem::Initialize SUCCESS!");
    return true;
}

void HairSystem::Shutdown(Renderer* renderer) {
    ClearAll(renderer);
}

std::shared_ptr<HairObject> HairSystem::SpawnHairstyle(HairHairstyleType type, const DirectX::XMFLOAT3& pos, Renderer* renderer) {
    uint32_t id = m_nextHairId++;
    std::string name;
    switch (type) {
        case HairHairstyleType::LongFlowing: name = "Long Flowing Hair"; break;
        case HairHairstyleType::Ponytail:    name = "High Ponytail"; break;
        case HairHairstyleType::BobCut:      name = "Bob Cut"; break;
        case HairHairstyleType::TwinTails:   name = "Twin Tails"; break;
        case HairHairstyleType::CurlyWavy:   name = "Curly Wavy Mane"; break;
        case HairHairstyleType::FurBall:     name = "Fur Ball"; break;
        default: name = "Custom Hairstyle"; break;
    }

    auto hair = std::make_shared<HairObject>(id, name);
    hair->SetHeadPosition(pos);
    hair->GenerateHairstyle(type);

    if (renderer) {
        DirectX::XMFLOAT3 camPos = renderer->GetCamera() ? renderer->GetCamera()->GetPosition() : DirectX::XMFLOAT3(0, 2.5f, 3.0f);
        hair->TessellateRibbons(camPos);
        hair->SyncToRenderer(renderer);
    }

    m_hairObjects.push_back(hair);
    m_selectedHairId = id;
    return hair;
}

std::shared_ptr<HairObject> HairSystem::GetHairObjectById(uint32_t id) {
    for (auto& h : m_hairObjects) {
        if (h->GetId() == id) return h;
    }
    return nullptr;
}

std::shared_ptr<HairObject> HairSystem::GetSelectedHair() {
    return GetHairObjectById(m_selectedHairId);
}

size_t HairSystem::GetTotalStrands() const {
    size_t total = 0;
    for (const auto& h : m_hairObjects) total += h->GetStrands().size();
    return total;
}

size_t HairSystem::GetTotalNodes() const {
    size_t total = 0;
    for (const auto& h : m_hairObjects) {
        for (const auto& s : h->GetStrands()) total += s.activeNodeCount;
    }
    return total;
}

void HairSystem::RemoveHairObject(uint32_t id, Renderer* renderer) {
    for (auto it = m_hairObjects.begin(); it != m_hairObjects.end(); ++it) {
        if ((*it)->GetId() == id) {
            if (renderer && (*it)->GetDynamicModelId() != 0) {
                renderer->RemoveDynamicModel((*it)->GetDynamicModelId());
            }
            m_hairObjects.erase(it);
            if (m_selectedHairId == id) {
                m_selectedHairId = m_hairObjects.empty() ? 0 : m_hairObjects.front()->GetId();
            }
            break;
        }
    }
}

void HairSystem::ClearAll(Renderer* renderer) {
    for (auto& h : m_hairObjects) {
        if (renderer && h->GetDynamicModelId() != 0) {
            renderer->RemoveDynamicModel(h->GetDynamicModelId());
        }
    }
    m_hairObjects.clear();
    m_selectedHairId = 0;
}

void HairSystem::ResetAll() {
    for (auto& h : m_hairObjects) {
        h->Reset();
    }
}

void HairSystem::Update(float dt, Renderer* renderer, Window* window, Camera* camera) {
    if (!renderer || m_hairObjects.empty()) return;

    m_simulationTime += dt;
    DirectX::XMFLOAT3 camPos = camera ? camera->GetPosition() : DirectX::XMFLOAT3(0, 2.5f, 3.0f);
    DirectX::XMFLOAT3 camDir = camera ? camera->GetForwardVector() : DirectX::XMFLOAT3(0, 0, -1);

    // 1. Head Motion Controller (Auto animation for dynamic physics showcase)
    if (m_headMotionEnabled && !m_isPaused) {
        for (auto& hair : m_hairObjects) {
            DirectX::XMFLOAT3 rot = hair->GetHeadRotation();
            float t = m_simulationTime * m_headMotionSpeed;

            switch (m_headMotionMode) {
                case 1: // Turn Left / Right Yaw swing
                    rot.y = std::sin(t) * m_headMotionAmplitude;
                    rot.z = std::cos(t * 0.5f) * (m_headMotionAmplitude * 0.2f);
                    break;
                case 2: // Head Nodding Pitch
                    rot.x = std::sin(t * 1.3f) * m_headMotionAmplitude;
                    break;
                case 3: // Circular Whip / Shake Head
                    rot.y = std::sin(t * 1.5f) * m_headMotionAmplitude;
                    rot.x = std::cos(t * 1.5f) * (m_headMotionAmplitude * 0.6f);
                    rot.z = std::sin(t * 3.0f) * (m_headMotionAmplitude * 0.35f);
                    break;
                case 4: // Breathing & Subtle Idle
                    rot.x = std::sin(t * 0.8f) * 4.0f;
                    rot.y = std::sin(t * 0.4f) * 6.0f;
                    break;
            }
            hair->SetHeadRotation(rot);
        }
    }

    // 2. Interactive Viewport Tools (Grab, Comb, Scissors)
    if (camera && window && !m_isPaused) {
#ifndef ENGINE_RELEASE
        bool isLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 && window->IsCursorEnabled() && !ImGui::GetIO().WantCaptureMouse;
#else
        bool isLeftDown = false;
#endif
        bool isGrabKey  = window->IsKeyDown('F') || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        bool isCutKey   = window->IsKeyDown('X');

        if (m_interactionMode == HairInteractionMode::Grab || isGrabKey) {
            if (isGrabKey || isLeftDown) {
                if (!m_isInteracting) {
                    float bestDist = 1e9f;
                    uint32_t hitHairId = 0;
                    uint32_t hitStrand = 0;
                    uint32_t hitNode = 0;
                    DirectX::XMFLOAT3 hitPos;

                    for (auto& hair : m_hairObjects) {
                        float dist;
                        uint32_t sIdx, nIdx;
                        DirectX::XMFLOAT3 hp;
                        if (hair->Raycast(camPos, camDir, dist, sIdx, nIdx, hp) && dist < bestDist) {
                            bestDist = dist;
                            hitHairId = hair->GetId();
                            hitStrand = sIdx;
                            hitNode = nIdx;
                            hitPos = hp;
                        }
                    }

                    if (hitHairId != 0 && bestDist < 30.0f) {
                        m_grabbedHairId = hitHairId;
                        m_grabbedStrandIdx = hitStrand;
                        m_grabbedNodeIdx = hitNode;
                        m_grabDistance = (std::clamp)(bestDist, 1.0f, 20.0f);
                        m_isInteracting = true;

                        auto hair = GetHairObjectById(m_grabbedHairId);
                        if (hair && m_grabbedStrandIdx < hair->GetStrands().size()) {
                            auto& strand = hair->GetStrands()[m_grabbedStrandIdx];
                            if (m_grabbedNodeIdx < strand.nodes.size()) {
                                strand.nodes[m_grabbedNodeIdx].isGrabbed = true;
                            }
                        }
                    }
                } else {
                    auto hair = GetHairObjectById(m_grabbedHairId);
                    if (hair && m_grabbedStrandIdx < hair->GetStrands().size()) {
                        auto& strand = hair->GetStrands()[m_grabbedStrandIdx];
                        if (m_grabbedNodeIdx < strand.nodes.size()) {
                            DirectX::XMFLOAT3 targetPos = HVecAdd(camPos, HVecMul(camDir, m_grabDistance));
                            strand.nodes[m_grabbedNodeIdx].predictedPosition = targetPos;
                            strand.nodes[m_grabbedNodeIdx].position = targetPos;
                            strand.nodes[m_grabbedNodeIdx].velocity = { 0.0f, 0.0f, 0.0f };
                        }
                    }
                }
            } else {
                if (m_isInteracting) {
                    auto hair = GetHairObjectById(m_grabbedHairId);
                    if (hair && m_grabbedStrandIdx < hair->GetStrands().size()) {
                        auto& strand = hair->GetStrands()[m_grabbedStrandIdx];
                        if (m_grabbedNodeIdx < strand.nodes.size()) {
                            strand.nodes[m_grabbedNodeIdx].isGrabbed = false;
                        }
                    }
                    m_isInteracting = false;
                    m_grabbedHairId = 0;
                }
            }
        }

        // Scissors Cut
        if (m_interactionMode == HairInteractionMode::Scissors || isCutKey) {
            if (isLeftDown || isCutKey) {
                for (auto& hair : m_hairObjects) {
                    hair->CutStrands(camPos, camDir, 30.0f);
                }
            }
        }

        // Comb / Brush
        if (m_interactionMode == HairInteractionMode::Comb && isLeftDown) {
            DirectX::XMFLOAT3 brushPoint = HVecAdd(camPos, HVecMul(camDir, 3.5f));
            for (auto& hair : m_hairObjects) {
                hair->BrushHair(brushPoint, camDir, 0.8f, 6.0f);
            }
        }
    }

    if (m_isPaused && !m_singleStep) return;

    auto tStart = std::chrono::high_resolution_clock::now();

    DirectX::XMFLOAT3 gravity = PhysicsSystem::Get().GetGravity();
    std::vector<HairCollider> externalColliders;

    // Collect scene colliders
    for (const auto& m : renderer->GetModels()) {
        if (!m.visible || !m.hasCollision || m.name == "Floor") continue;
        HairCollider col;
        col.position = m.position;
        if (m.name.find("Sphere") != std::string::npos) {
            col.type = HairColliderType::Sphere;
            col.radius = m.scale.x * 0.5f;
            externalColliders.push_back(col);
        }
    }

    // Step physics & Tessellate ribbons
    for (auto& hair : m_hairObjects) {
        DirectX::XMFLOAT3 wind = { 1.5f, 0.2f, 0.8f }; // Default gentle ambient breeze
        hair->StepPhysics(dt, gravity, wind, externalColliders);
        hair->TessellateRibbons(camPos);
        hair->SyncToRenderer(renderer);
    }

    if (m_singleStep) {
        m_singleStep = false;
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    m_lastSimTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
}

// -----------------------------------------------------------------------------------------
// ImGui UI Control Studio
// -----------------------------------------------------------------------------------------

void HairSystem::DrawUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!renderer) return;

    ImGui::Begin("Hair Physics Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "XPBD Cosserat Strand Engine | Active Objects: %zu", m_hairObjects.size());
    ImGui::Text("Total Strands: %zu | Nodes: %zu | Sim Step: %.3f ms",
                GetTotalStrands(), GetTotalNodes(), m_lastSimTimeMs);

    ImGui::Separator();

    // 1. Playback controls
    if (m_isPaused) {
        if (ImGui::Button(" Resume")) m_isPaused = false;
    } else {
        if (ImGui::Button(" Pause")) m_isPaused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(" Step Frame")) SingleStep();
    ImGui::SameLine();
    if (ImGui::Button(" Reset All")) ResetAll();
    ImGui::SameLine();
    if (ImGui::Button(" Clear All")) ClearAll(renderer);

    ImGui::Separator();

    // 2. Interactive Tool Selector
    ImGui::Text("Interactive Viewport Tools:");
    int toolIdx = (int)m_interactionMode;
    const char* tools[] = { " Inspect", " Grab & Pull Hair (F)", " Hair Comb / Brush", " Hair Scissors (X)" };
    if (ImGui::Combo("Tool Mode", &toolIdx, tools, IM_ARRAYSIZE(tools))) {
        m_interactionMode = (HairInteractionMode)toolIdx;
    }

    ImGui::Separator();

    // 3. Head Motion Controller
    if (ImGui::TreeNode("Head Motion & Turntable")) {
        ImGui::Checkbox("Enable Dynamic Head Motion", &m_headMotionEnabled);
        if (m_headMotionEnabled) {
            const char* motions[] = { "Static", "Turn Left / Right", "Head Nodding", "Circular Whip / Shake", "Breathing Idle" };
            ImGui::Combo("Motion Type", &m_headMotionMode, motions, IM_ARRAYSIZE(motions));
            ImGui::SliderFloat("Motion Speed", &m_headMotionSpeed, 0.2f, 8.0f, "%.2fx");
            ImGui::SliderFloat("Motion Amplitude", &m_headMotionAmplitude, 5.0f, 90.0f, "%.1f°");
        }

        auto curHair = GetSelectedHair();
        if (curHair) {
            DirectX::XMFLOAT3 hPos = curHair->GetHeadPosition();
            DirectX::XMFLOAT3 hRot = curHair->GetHeadRotation();
            float hp[3] = { hPos.x, hPos.y, hPos.z };
            float hr[3] = { hRot.x, hRot.y, hRot.z };

            if (ImGui::DragFloat3("Manual Head Position", hp, 0.05f, -20.0f, 20.0f, "%.2f")) {
                curHair->SetHeadPosition({ hp[0], hp[1], hp[2] });
            }
            if (ImGui::SliderFloat3("Manual Head Rotation", hr, -90.0f, 90.0f, "%.1f°")) {
                curHair->SetHeadRotation({ hr[0], hr[1], hr[2] });
            }
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    // 4. Spawner & Hairstyle Presets
    if (ImGui::TreeNode("Spawn Hairstyle Presets")) {
        const char* presets[] = { "Long Flowing", "High Ponytail", "Bob Cut", "Twin Tails", "Curly Wavy", "Fur Ball" };
        ImGui::Combo("Hairstyle Preset", &m_spawnPresetIdx, presets, IM_ARRAYSIZE(presets));
        ImGui::DragFloat3("Spawn Position", m_spawnPos, 0.1f, -50.0f, 50.0f, "%.2f");

        if (camera && ImGui::Button(" Position in Front of Camera")) {
            DirectX::XMFLOAT3 cPos = camera->GetPosition();
            DirectX::XMFLOAT3 cFwd = camera->GetForwardVector();
            m_spawnPos[0] = cPos.x + cFwd.x * 3.5f;
            m_spawnPos[1] = (std::max)(cPos.y + cFwd.y * 3.5f, 1.2f);
            m_spawnPos[2] = cPos.z + cFwd.z * 3.5f;
        }

        if (ImGui::Button(" Spawn Hairstyle")) {
            DirectX::XMFLOAT3 sp = { m_spawnPos[0], m_spawnPos[1], m_spawnPos[2] };
            SpawnHairstyle((HairHairstyleType)m_spawnPresetIdx, sp, renderer);
        }

        ImGui::TreePop();
    }

    ImGui::Separator();

    // 5. Selected Hairstyle Inspector & Styling
    auto hair = GetSelectedHair();
    if (hair) {
        if (ImGui::TreeNode("Hairstyle Properties & Grooming")) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Editing: %s [ID %u]", hair->GetName().c_str(), hair->GetId());

            // Hair Selector Combo
            std::vector<std::string> names;
            int curIdx = 0;
            for (size_t i = 0; i < m_hairObjects.size(); ++i) {
                names.push_back(m_hairObjects[i]->GetName() + " (" + std::to_string(m_hairObjects[i]->GetId()) + ")");
                if (m_hairObjects[i]->GetId() == m_selectedHairId) curIdx = (int)i;
            }
            if (!names.empty() && ImGui::BeginCombo("Select Hair", names[curIdx].c_str())) {
                for (size_t i = 0; i < names.size(); ++i) {
                    bool isSelected = (curIdx == (int)i);
                    if (ImGui::Selectable(names[i].c_str(), isSelected)) {
                        m_selectedHairId = m_hairObjects[i]->GetId();
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Hairstyle Type Switcher
            int styleIdx = (int)hair->GetHairstyleType();
            const char* presets[] = { "Long Flowing", "High Ponytail", "Bob Cut", "Twin Tails", "Curly Wavy", "Fur Ball" };
            if (ImGui::Combo("Hairstyle Type", &styleIdx, presets, IM_ARRAYSIZE(presets))) {
                hair->GenerateHairstyle((HairHairstyleType)styleIdx);
            }

            // Color Palette Selector
            ImGui::Separator();
            const char* palettes[] = { "Brunette", "Blonde", "Raven Black", "Crimson Red", "Platinum Silver", "Pastel Pink", "Cyan" };
            static int s_colorPalIdx = 0;
            if (ImGui::Combo("Color Palette", &s_colorPalIdx, palettes, IM_ARRAYSIZE(palettes))) {
                hair->ApplyColorPreset(s_colorPalIdx);
            }

            HairConfig& cfg = hair->GetConfig();
            float rootC[3] = { cfg.rootColor.x, cfg.rootColor.y, cfg.rootColor.z };
            float tipC[3]  = { cfg.tipColor.x, cfg.tipColor.y, cfg.tipColor.z };
            if (ImGui::ColorEdit3("Root Color", rootC)) cfg.rootColor = { rootC[0], rootC[1], rootC[2] };
            if (ImGui::ColorEdit3("Tip Color", tipC)) {
                cfg.tipColor = { tipC[0], tipC[1], tipC[2] };
                hair->GetMaterial().albedo = cfg.tipColor;
            }

            ImGui::Separator();
            ImGui::Text("Grooming & Strand Density:");
            bool regen = false;
            if (ImGui::SliderInt("Strand Count", &cfg.numStrands, 20, 500)) regen = true;
            if (ImGui::SliderInt("Nodes per Strand", &cfg.nodesPerStrand, 6, 24)) regen = true;
            if (ImGui::SliderFloat("Hair Length", &cfg.hairLength, 0.2f, 2.0f, "%.2f m")) regen = true;
            if (regen) hair->GenerateHairstyle(hair->GetHairstyleType());

            ImGui::SliderFloat("Root Width", &cfg.rootWidth, 0.005f, 0.05f, "%.3f m");
            ImGui::SliderFloat("Tip Width", &cfg.tipWidth, 0.001f, 0.02f, "%.3f m");

            ImGui::Separator();
            ImGui::Text("Physics & Dynamics:");
            ImGui::SliderFloat("Stretch Stiffness", &cfg.stretchStiffness, 0.5f, 1.0f, "%.3f");
            ImGui::SliderFloat("Bend Stiffness", &cfg.bendStiffness, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Shape Retention", &cfg.shapeRetention, 0.0f, 1.0f, "%.3f");
            ImGui::SliderFloat("Damping", &cfg.damping, 0.90f, 0.999f, "%.4f");
            ImGui::SliderFloat("Air Drag", &cfg.airDrag, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("Gravity Multiplier", &cfg.gravityMultiplier, -3.0f, 3.0f, "%.2f");
            ImGui::SliderInt("Sub-steps", &cfg.subSteps, 1, 8);
            ImGui::SliderInt("Solver Iterations", &cfg.solverIterations, 1, 25);

            ImGui::Separator();
            ImGui::Text("Collision Geometry:");
            ImGui::SliderFloat("Head Radius X", &cfg.headRadiusX, 0.2f, 0.7f, "%.2f m");
            ImGui::SliderFloat("Head Radius Y", &cfg.headRadiusY, 0.2f, 0.7f, "%.2f m");
            ImGui::SliderFloat("Head Radius Z", &cfg.headRadiusZ, 0.2f, 0.7f, "%.2f m");
            ImGui::SliderFloat("Neck Radius", &cfg.neckRadius, 0.1f, 0.4f, "%.2f m");
            ImGui::SliderFloat("Shoulder Width", &cfg.shoulderWidth, 0.4f, 1.5f, "%.2f m");

            ImGui::Separator();
            if (ImGui::Button(" Reset Hairstyle")) hair->Reset();
            ImGui::SameLine();
            if (ImGui::Button(" Delete Hairstyle")) {
                RemoveHairObject(hair->GetId(), renderer);
            }

            ImGui::TreePop();
        }
    }

    ImGui::End();
#endif
}

} // namespace EnginePhysics
