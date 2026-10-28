#pragma once

#include "Platform/PlatformTypes.h"
#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "Platform/DirectXMath/DirectXMath.h"
#endif
#include <memory>

namespace physx {
    class PxPhysics;
    class PxScene;
    class PxMaterial;
    class PxControllerManager;
    class PxController;
    class PxCapsuleController;
}

namespace EnginePhysics {

struct CharacterConfig {
    float height = 1.4f;           // Capsule cylinder height
    float radius = 0.4f;           // Capsule cap radius (total height = height + 2 * radius = 2.2m)
    float stepOffset = 0.35f;       // Auto-climb step height (stairs)
    float slopeLimitDegrees = 45.0f;// Max walkable slope angle
    float walkSpeed = 6.0f;        // Normal walk speed
    float runSpeed = 11.0f;        // Sprint speed
    float crouchSpeed = 3.0f;      // Crouch speed
    float jumpForce = 8.5f;        // Jump velocity (m/s)
    float gravity = -20.0f;        // Gravity acceleration (m/s^2)
    float pushForce = 35.0f;       // Push force applied to dynamic physics actors
};

class CharacterController {
public:
    static CharacterController& Get();

    bool Initialize(physx::PxPhysics* physics, physx::PxScene* scene, physx::PxMaterial* material);
    void Shutdown();

    // Position & Teleport
    void SetPosition(float x, float y, float z);
    void SetPosition(const DirectX::XMFLOAT3& pos) { SetPosition(pos.x, pos.y, pos.z); }
    DirectX::XMFLOAT3 GetPosition() const;
    DirectX::XMFLOAT3 GetEyePosition() const; // Position + eye height for camera

    // Movement & Kinematic Update
    void Update(float dt, const DirectX::XMFLOAT3& moveDir, bool wantJump, bool isRunning, bool isCrouching);

    // Collision Flags
    bool IsGrounded() const { return m_isGrounded; }
    bool IsTouchingCeiling() const { return m_touchingCeiling; }
    bool IsTouchingWall() const { return m_touchingWall; }

    // Configuration & Tuning
    CharacterConfig& GetConfig() { return m_config; }
    const CharacterConfig& GetConfig() const { return m_config; }
    void ApplyConfig();

    // Enable/Disable Kinematic Simulation (e.g. during Freecam noclip)
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled && m_controller != nullptr; }

    // Velocity query
    DirectX::XMFLOAT3 GetVelocity() const { return m_velocity; }
    float GetCurrentSpeed() const;

    // ImGui Tuning Panel
    void DrawUI();

private:
    CharacterController();
    ~CharacterController();
    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;

    class HitReport;
    std::unique_ptr<HitReport> m_hitReport;

    physx::PxControllerManager* m_manager = nullptr;
    physx::PxController* m_controller = nullptr;
    physx::PxCapsuleController* m_capsule = nullptr;
    physx::PxMaterial* m_material = nullptr;
    physx::PxScene* m_scene = nullptr;
    physx::PxPhysics* m_physics = nullptr;

    CharacterConfig m_config;
    DirectX::XMFLOAT3 m_velocity = { 0.0f, 0.0f, 0.0f };
    float m_verticalVelocity = 0.0f;
    bool m_isGrounded = false;
    bool m_touchingCeiling = false;
    bool m_touchingWall = false;
    bool m_enabled = true;
    bool m_isCrouching = false;
    float m_coyoteTime = 0.0f;
    float m_jumpBuffer = 0.0f;
};

} // namespace EnginePhysics
