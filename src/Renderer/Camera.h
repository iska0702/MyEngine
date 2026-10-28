#pragma once
#include <DirectXMath.h>
#include <vector>

using namespace DirectX;

class Camera {
public:
    Camera();
    ~Camera();

    void SetPosition(float x, float y, float z);
    void SetPosition(const XMFLOAT3& pos) { SetPosition(pos.x, pos.y, pos.z); }
    void MoveForward(float d);
    void MoveRight(float d);
    void MoveUp(float d);

    void AddPitch(float p);
    void AddYaw(float y);
    float GetYaw() const { return m_yaw; }
    void SetYaw(float yaw) { m_yaw = yaw; UpdateVectors(); }
    float GetPitch() const { return m_pitch; }
    void SetPitch(float pitch) { m_pitch = pitch; UpdateVectors(); }

    void Update(float deltaTime, bool simulateMovement = true);
    void SetControllerPose(const XMFLOAT3& eyePosition, bool grounded);
    void Jitter(int frameIndex, int width, int height); // Apply Halton sequence
    void Jump(float force);
    void SetCrouching(bool isCrouching);

    struct CollisionBox {
        XMFLOAT3 minPoint;
        XMFLOAT3 maxPoint;
    };

    void SetCollisionBoxes(std::vector<CollisionBox> boxes) { m_collisionBoxes = std::move(boxes); }
    const std::vector<CollisionBox>& GetCollisionBoxes() const { return m_collisionBoxes; }
    void SetCollisionEnabled(bool enabled) { m_collisionEnabled = enabled; }
    void SetFloorEnabled(bool enabled) { m_floorEnabled = enabled; }
    void SetFloorY(float y) { m_floorY = y; }
    float GetFloorY() const { return m_floorY; }
    void SetKillPlaneY(float y) { m_killPlaneY = y; }
    float GetKillPlaneY() const { return m_killPlaneY; }

    void SetFreecamMode(bool enabled);
    bool IsFreecamMode() const { return m_isFreecam; }

    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane) const;
    XMMATRIX GetCleanProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane) const;

    // Temporal Upscaling Support
    void SetJitter(float x, float y) { m_jitterX = x; m_jitterY = y; }
    XMFLOAT2 GetJitter() const { return {m_jitterX, m_jitterY}; }
    float GetJitterX() const { return m_jitterX; }
    float GetJitterY() const { return m_jitterY; }
    XMMATRIX GetPreviousViewProjMatrix() const { return m_prevViewProj; }
    void UpdatePreviousMatrix(const XMMATRIX& viewProj) { m_prevViewProj = viewProj; }

    XMFLOAT3 GetPosition() const { return m_position; }
    XMFLOAT3 GetUpVector() const { return m_up; }
    XMFLOAT3 GetRightVector() const { return m_right; }
    XMFLOAT3 GetForwardVector() const { return m_forward; }
    XMFLOAT3 GetForward() const { return m_forward; }

private:
    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
    XMMATRIX m_prevViewProj = XMMatrixIdentity();
    void UpdateVectors();

    XMFLOAT3 m_position;
    XMFLOAT3 m_forward;
    XMFLOAT3 m_right;
    XMFLOAT3 m_up;

    float m_pitch = 0.0f;
    float m_yaw = 0.0f;
    // Physics
    XMFLOAT3 m_velocity = {0.0f, 0.0f, 0.0f};
    float m_gravity = -20.0f;
    bool m_isGrounded = false;
    bool m_isCrouching = false;
    bool m_collisionEnabled = true;
    bool m_floorEnabled = true;
    float m_floorY = 0.0f;
    float m_killPlaneY = -500.0f;

    // Freecam
    bool m_isFreecam = false;
    XMFLOAT3 m_characterPos = {0.0f, 0.0f, 0.0f};
    float m_characterPitch = 0.0f;
    float m_characterYaw = 0.0f;
    float m_currentEyeHeight = 1.6f;
    float m_bobTime = 0.0f;
    float m_bobMagnitude = 0.0f;
    XMFLOAT3 m_lastPos = {0.0f, 0.0f, 0.0f};
    
    float m_standHeight = 1.6f;
    float m_crouchHeight = 0.8f;

    std::vector<CollisionBox> m_collisionBoxes;

    bool CheckCollision(const XMFLOAT3& targetPos) const;
};
