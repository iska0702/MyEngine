#include "PortalSystem.h"
#include "WorldDimensionSystem.h"
#include "Renderer.h"
#include <cmath>
#include <algorithm>

#ifndef ENGINE_RELEASE
#include "imgui.h"
#endif

using namespace DirectX;

// Helper to construct rotation basis from normal
void Portal::UpdateBasis(const XMFLOAT3& inNormal) {
    XMVECTOR N = XMVector3Normalize(XMLoadFloat3(&inNormal));
    XMStoreFloat3(&normal, N);

    XMVECTOR refUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(normal.y) > 0.92f) {
        refUp = XMVectorSet(0.0f, 0.0f, normal.y > 0.0f ? -1.0f : 1.0f, 0.0f);
    }

    XMVECTOR R = XMVector3Normalize(XMVector3Cross(refUp, N));
    XMVECTOR U = XMVector3Normalize(XMVector3Cross(N, R));

    XMStoreFloat3(&right, R);
    XMStoreFloat3(&up, U);
}

XMMATRIX Portal::GetWorldMatrix() const {
    XMVECTOR r = XMLoadFloat3(&right);
    XMVECTOR u = XMLoadFloat3(&up);
    XMVECTOR n = XMLoadFloat3(&normal);
    XMVECTOR p = XMLoadFloat3(&position);

    XMMATRIX mat;
    mat.r[0] = r;
    mat.r[1] = u;
    mat.r[2] = n;
    mat.r[3] = XMVectorSetW(p, 1.0f);
    return mat;
}

PortalSystem& PortalSystem::Get() {
    static PortalSystem instance;
    return instance;
}

void PortalSystem::Initialize(Renderer* renderer) {
    if (m_initialized) return;

    // Portal A - Minecraft Nether Portal (Surface)
    m_portals[0].id = PortalID::Blue_A;
    m_portals[0].name = "Nether Portal A";
    m_portals[0].color = XMFLOAT4(0.65f, 0.15f, 0.95f, 1.0f);
    m_portals[0].position = XMFLOAT3(0.0f, 2.5f, 4.5f);
    m_portals[0].UpdateBasis(XMFLOAT3(0.0f, 0.0f, -1.0f));
    m_portals[0].isPlaced = false;

    // Portal B - Minecraft Nether Portal (Underworld)
    m_portals[1].id = PortalID::Orange_B;
    m_portals[1].name = "Nether Portal B";
    m_portals[1].color = XMFLOAT4(0.75f, 0.12f, 0.85f, 1.0f);
    m_portals[1].position = XMFLOAT3(0.0f, -117.5f, 4.5f);
    m_portals[1].UpdateBasis(XMFLOAT3(0.0f, 0.0f, -1.0f));
    m_portals[1].isPlaced = false;

    m_teleportCooldown = 0.0f;
    m_initialized = true;
}

void PortalSystem::Update(float dt, Camera* camera, Renderer* renderer) {
    if (m_teleportCooldown > 0.0f) {
        m_teleportCooldown -= dt;
    }

    if (camera) {
        CheckAndTeleportPlayer(camera, dt, renderer);
        m_prevCameraPos = camera->GetPosition();
    }
}

void PortalSystem::PlacePortal(PortalID id, const XMFLOAT3& hitPos, const XMFLOAT3& hitNormal) {
    size_t idx = static_cast<size_t>(id);
    auto& p = m_portals[idx];
    p.UpdateBasis(hitNormal);
    
    // Offset slightly forward along normal so portal quad sits just above the wall/floor surface
    p.position.x = hitPos.x + p.normal.x * 0.04f;
    p.position.y = hitPos.y + p.normal.y * 0.04f;
    p.position.z = hitPos.z + p.normal.z * 0.04f;
    p.isPlaced = true;
}

void PortalSystem::ResetPortals() {
    m_portals[0].isPlaced = false;
    m_portals[1].isPlaced = false;
    m_teleportCooldown = 0.0f;
}

XMMATRIX PortalSystem::ComputeRelativeTransform(const Portal& inPortal, const Portal& outPortal) {
    XMMATRIX matIn = inPortal.GetWorldMatrix();
    XMMATRIX matOut = outPortal.GetWorldMatrix();

    XMVECTOR det;
    XMMATRIX invIn = XMMatrixInverse(&det, matIn);

    // 180-degree rotation around Up axis (Y) to reverse local heading
    XMMATRIX rot180 = XMMatrixRotationY(XM_PI);

    // M_rel = Inv(MatIn) * Rot180 * MatOut
    return invIn * rot180 * matOut;
}

XMMATRIX PortalSystem::ComputeVirtualViewMatrix(const Portal& inPortal, const Portal& outPortal,
                                                const XMMATRIX& camViewMatrix,
                                                const XMFLOAT3& camPos) {
    XMMATRIX relTransform = ComputeRelativeTransform(inPortal, outPortal);

    // Transform camera position
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    XMVECTOR virtPosVec = XMVector3TransformCoord(camPosVec, relTransform);

    // Deconstruct camera view matrix to obtain forward and up vectors
    XMVECTOR det;
    XMMATRIX invView = XMMatrixInverse(&det, camViewMatrix);
    XMVECTOR camForward = invView.r[2];
    XMVECTOR camUp = invView.r[1];

    // Transform direction vectors (normal transform)
    XMVECTOR virtForward = XMVector3Normalize(XMVector3TransformNormal(camForward, relTransform));
    XMVECTOR virtUp = XMVector3Normalize(XMVector3TransformNormal(camUp, relTransform));

    return XMMatrixLookToLH(virtPosVec, virtForward, virtUp);
}

bool PortalSystem::CheckAndTeleportPlayer(Camera* camera, float dt, Renderer* renderer) {
    if (!camera || !AreBothPortalsPlaced() || m_teleportCooldown > 0.0f) {
        return false;
    }

    XMFLOAT3 curPos = camera->GetPosition();

    for (size_t i = 0; i < 2; ++i) {
        const auto& inPortal = m_portals[i];
        const auto& outPortal = m_portals[1 - i];

        XMVECTOR N = XMLoadFloat3(&inPortal.normal);
        XMVECTOR P0 = XMLoadFloat3(&inPortal.position);
        XMVECTOR P_prev = XMLoadFloat3(&m_prevCameraPos);
        XMVECTOR P_cur = XMLoadFloat3(&curPos);

        // Distance from plane: dot(P - P0, N)
        float d_prev = XMVectorGetX(XMVector3Dot(P_prev - P0, N));
        float d_cur  = XMVectorGetX(XMVector3Dot(P_cur - P0, N));

        // Player crossed from front (d_prev > -0.1f) to back (d_cur <= 0.05f)
        if (d_prev > -0.15f && d_cur <= 0.05f) {
            float denom = (d_prev - d_cur);
            float t = (fabsf(denom) > 1e-4f) ? (d_prev / denom) : 0.0f;
            t = std::clamp(t, 0.0f, 1.0f);

            XMVECTOR P_int = XMVectorLerp(P_prev, P_cur, t);
            XMVECTOR delta = P_int - P0;

            XMVECTOR R = XMLoadFloat3(&inPortal.right);
            XMVECTOR U = XMLoadFloat3(&inPortal.up);

            float localX = XMVectorGetX(XMVector3Dot(delta, R));
            float localY = XMVectorGetX(XMVector3Dot(delta, U));

            // Elliptical portal aperture test
            float normX = localX / (inPortal.width * 0.5f);
            float normY = localY / (inPortal.height * 0.5f);
            if ((normX * normX + normY * normY) <= 1.25f) {
                // Inside portal bounds -> TELEPORT!
                XMMATRIX relMat = ComputeRelativeTransform(inPortal, outPortal);

                // Compute new position
                XMVECTOR newPosVec = XMVector3TransformCoord(P_cur, relMat);
                // Step forward out of exit portal along normal to prevent immediate reverse teleport
                XMVECTOR exitNormal = XMLoadFloat3(&outPortal.normal);
                newPosVec = newPosVec + exitNormal * 0.40f;

                XMFLOAT3 newPos;
                XMStoreFloat3(&newPos, newPosVec);
                camera->SetPosition(newPos);

                // Conserve and reorient view direction
                XMFLOAT3 curForward = camera->GetForwardVector();
                XMVECTOR oldF = XMLoadFloat3(&curForward);
                XMVECTOR newF = XMVector3Normalize(XMVector3TransformNormal(oldF, relMat));
                
                XMFLOAT3 fDir;
                XMStoreFloat3(&fDir, newF);

                float newYaw = XMConvertToDegrees(atan2f(fDir.x, fDir.z));
                float newPitch = XMConvertToDegrees(asinf(std::clamp(fDir.y, -0.999f, 0.999f)));

                camera->SetYaw(newYaw);
                camera->SetPitch(newPitch);

                m_prevCameraPos = newPos;
                m_teleportCooldown = 0.25f; // 250ms debounce threshold

                if (inPortal.id == PortalID::Blue_A) {
                    WorldDimensionSystem::Get().OnPlayerEnteredPortal(renderer, camera);
                }

                return true;
            }
        }
    }

    return false;
}

void PortalSystem::DrawPortalUI(Renderer* renderer, Camera* camera) {
#ifndef ENGINE_RELEASE
    if (!m_showPortalWindow) return;

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Portal System & Controls", &m_showPortalWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.0f, 0.75f, 1.0f, 1.0f), "LINKED NON-EUCLIDEAN PORTAL SYSTEM");
    ImGui::Separator();
    
    ImGui::Checkbox("Portal Gun Active", &m_portalGunActive);
    ImGui::SameLine();
    ImGui::TextDisabled("(Key '1': Portal A, Key '2': Portal B)");
    
    ImGui::Spacing();
    if (ImGui::Button("Reset All Portals")) {
        ResetPortals();
    }
    ImGui::SameLine();
    if (ImGui::Button("Place Pair in Front of View") && camera) {
        XMFLOAT3 pos = camera->GetPosition();
        XMFLOAT3 fwd = camera->GetForwardVector();
        
        // Place Portal A 4m ahead, facing back to player
        XMFLOAT3 posA = { pos.x + fwd.x * 4.0f, pos.y, pos.z + fwd.z * 4.0f };
        XMFLOAT3 normA = { -fwd.x, 0.0f, -fwd.z };
        PlacePortal(PortalID::Blue_A, posA, normA);

        // Place Portal B 10m ahead, facing back to player
        XMFLOAT3 posB = { pos.x + fwd.x * 10.0f, pos.y, pos.z + fwd.z * 10.0f };
        XMFLOAT3 normB = { -fwd.x, 0.0f, -fwd.z };
        PlacePortal(PortalID::Orange_B, posB, normB);
    }

    ImGui::Separator();
    
    for (size_t i = 0; i < 2; ++i) {
        auto& p = m_portals[i];
        ImGui::PushID((int)i);

        ImVec4 col = ImVec4(p.color.x, p.color.y, p.color.z, 1.0f);
        ImGui::TextColored(col, "%s %s", p.name.c_str(), p.isPlaced ? "[PLACED / ACTIVE]" : "[UNPLACED]");

        if (ImGui::Button("Place at Current View Position") && camera) {
            XMFLOAT3 pos = camera->GetPosition();
            XMFLOAT3 fwd = camera->GetForwardVector();
            PlacePortal(p.id, XMFLOAT3(pos.x + fwd.x * 3.0f, pos.y, pos.z + fwd.z * 3.0f), XMFLOAT3(-fwd.x, -fwd.y, -fwd.z));
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            p.isPlaced = false;
        }

        if (p.isPlaced) {
            ImGui::DragFloat3("Position", &p.position.x, 0.1f);
            if (ImGui::DragFloat3("Normal", &p.normal.x, 0.05f, -1.0f, 1.0f)) {
                p.UpdateBasis(p.normal);
            }
            ImGui::SliderFloat("Width", &p.width, 0.5f, 4.0f, "%.2f m");
            ImGui::SliderFloat("Height", &p.height, 1.0f, 6.0f, "%.2f m");
            ImGui::ColorEdit3("Portal Color", &p.color.x);
            ImGui::SliderFloat("Glow Intensity", &p.glowIntensity, 0.5f, 10.0f);
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    // Status Summary
    if (AreBothPortalsPlaced()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "STATUS: PORTAL LINK ESTABLISHED (PASSAGE OPEN)");
        ImGui::Text("Debounce Cooldown: %.2f s", (std::max)(0.0f, m_teleportCooldown));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "STATUS: WAITING FOR BOTH PORTALS TO BE PLACED");
    }

    ImGui::End();
#endif
}
