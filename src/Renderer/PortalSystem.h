#pragma once

#include <DirectXMath.h>
#include <string>
#include <array>
#include <memory>
#include "Camera.h"

class Renderer;

enum class PortalID {
    Blue_A = 0,
    Orange_B = 1
};

struct Portal {
    PortalID id = PortalID::Blue_A;
    std::string name;
    bool isPlaced = false;
    
    DirectX::XMFLOAT3 position = { 0.0f, 1.2f, 0.0f };
    DirectX::XMFLOAT3 normal   = { 0.0f, 0.0f, 1.0f }; // Faces out of the portal
    DirectX::XMFLOAT3 up       = { 0.0f, 1.0f, 0.0f };
    DirectX::XMFLOAT3 right    = { 1.0f, 0.0f, 0.0f };
    
    float width  = 2.0f; // Standard Minecraft 2-block portal width
    float height = 3.0f; // Standard Minecraft 3-block portal height

    DirectX::XMFLOAT4 color = { 0.65f, 0.15f, 0.95f, 1.0f }; // Classic Minecraft Nether Portal Purple
    float glowIntensity = 3.5f;

    DirectX::XMMATRIX GetWorldMatrix() const;
    void UpdateBasis(const DirectX::XMFLOAT3& inNormal);
};

class PortalSystem {
public:
    static PortalSystem& Get();

    void Initialize(Renderer* renderer);
    void Update(float dt, Camera* camera, Renderer* renderer = nullptr);

    void PlacePortal(PortalID id, const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitNormal);
    void ResetPortals();

    bool AreBothPortalsPlaced() const { return m_portals[0].isPlaced && m_portals[1].isPlaced; }
    const Portal& GetPortal(PortalID id) const { return m_portals[static_cast<size_t>(id)]; }
    Portal& GetPortal(PortalID id) { return m_portals[static_cast<size_t>(id)]; }

    // Transformation & Virtual Camera Mathematics
    static DirectX::XMMATRIX ComputeRelativeTransform(const Portal& inPortal, const Portal& outPortal);
    static DirectX::XMMATRIX ComputeVirtualViewMatrix(const Portal& inPortal, const Portal& outPortal, 
                                                      const DirectX::XMMATRIX& camViewMatrix, 
                                                      const DirectX::XMFLOAT3& camPos);

    // Continuous Teleportation detection & momentum/view conservation
    bool CheckAndTeleportPlayer(Camera* camera, float dt, Renderer* renderer = nullptr);

    // Editor & In-Game UI
    void DrawPortalUI(Renderer* renderer, Camera* camera);

    bool IsPortalWindowOpen() const { return m_showPortalWindow; }
    void SetPortalWindowOpen(bool open) { m_showPortalWindow = open; }

    bool IsPortalGunActive() const { return m_portalGunActive; }
    void SetPortalGunActive(bool active) { m_portalGunActive = active; }

    float GetTeleportCooldown() const { return m_teleportCooldown; }

private:
    PortalSystem() = default;
    ~PortalSystem() = default;

    std::array<Portal, 2> m_portals;
    DirectX::XMFLOAT3 m_prevCameraPos = { 0.0f, 0.0f, 0.0f };
    float m_teleportCooldown = 0.0f;
    bool m_initialized = false;
    bool m_showPortalWindow = true;
    bool m_portalGunActive = true;
};
