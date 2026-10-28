#include "Camera.h"
#include <algorithm>
#include <stdexcept>

Camera::Camera()
    : m_position(0.0f, 1.6f, -5.0f)
    , m_forward(0.0f, 0.0f, 1.0f)
    , m_right(1.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_pitch(0.0f)
    , m_yaw(0.0f)
    , m_velocity(0.0f, 0.0f, 0.0f)
    , m_gravity(-20.0f)
    , m_isGrounded(true)
    , m_isCrouching(false)
    , m_currentEyeHeight(1.6f)
    , m_bobTime(0.0f)
    , m_bobMagnitude(0.0f) {
    m_lastPos = m_position;
    UpdateVectors();
}

Camera::~Camera() {}

void Camera::SetPosition(float x, float y, float z) {
    m_position = XMFLOAT3(x, y, z);
}

void Camera::SetFreecamMode(bool enabled) {
    if (m_isFreecam == enabled) return;
    
    m_isFreecam = enabled;
    if (enabled) {
        // Enter freecam
        m_characterPos = m_position;
        m_characterPitch = m_pitch;
        m_characterYaw = m_yaw;
    } else {
        // Exit freecam
        m_position = m_characterPos;
        m_pitch = m_characterPitch;
        m_yaw = m_characterYaw;
        UpdateVectors();
        m_velocity = XMFLOAT3(0, 0, 0);
    }
}

void Camera::MoveForward(float d) {
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR p = XMLoadFloat3(&m_position);
    XMVECTOR f = XMLoadFloat3(&m_forward);
    // Move on XZ plane only, unless in freecam
    if (!m_isFreecam) {
        f = XMVectorSetY(f, 0.0f);
        XMVECTOR lenSq = XMVector3LengthSq(f);
        if (XMVectorGetX(lenSq) < 1e-6f) {
            f = XMVectorSet(sinf(m_yaw), 0.0f, cosf(m_yaw), 0.0f);
        } else {
            f = XMVector3Normalize(f);
        }
    } else {
        f = XMVector3Normalize(f);
    }
    
    XMVECTOR nextP = XMVectorMultiplyAdd(s, f, p);
    XMFLOAT3 targetPos;
    XMStoreFloat3(&targetPos, nextP);

    if (m_isFreecam) {
        XMStoreFloat3(&m_position, nextP);
    } else {
        if (!CheckCollision(targetPos)) {
            XMStoreFloat3(&m_position, nextP);
        } else {
            // Slide along X
            XMFLOAT3 posX = { targetPos.x, m_position.y, m_position.z };
            if (!CheckCollision(posX)) {
                m_position.x = posX.x;
            }
            // Slide along Z
            XMFLOAT3 posZ = { m_position.x, m_position.y, targetPos.z };
            if (!CheckCollision(posZ)) {
                m_position.z = posZ.z;
            }
        }
    }
}

void Camera::MoveRight(float d) {
    XMVECTOR s = XMVectorReplicate(d);
    XMVECTOR p = XMLoadFloat3(&m_position);
    XMVECTOR r = XMLoadFloat3(&m_right);
    // Move on XZ plane only, unless in freecam
    if (!m_isFreecam) {
        r = XMVectorSetY(r, 0.0f);
        XMVECTOR lenSq = XMVector3LengthSq(r);
        if (XMVectorGetX(lenSq) < 1e-6f) {
            r = XMVectorSet(cosf(m_yaw), 0.0f, -sinf(m_yaw), 0.0f);
        } else {
            r = XMVector3Normalize(r);
        }
    } else {
        r = XMVector3Normalize(r);
    }
    
    XMVECTOR nextP = XMVectorMultiplyAdd(s, r, p);
    XMFLOAT3 targetPos;
    XMStoreFloat3(&targetPos, nextP);

    if (m_isFreecam) {
        XMStoreFloat3(&m_position, nextP);
    } else {
        if (!CheckCollision(targetPos)) {
            XMStoreFloat3(&m_position, nextP);
        } else {
            // Slide along X
            XMFLOAT3 posX = { targetPos.x, m_position.y, m_position.z };
            if (!CheckCollision(posX)) {
                m_position.x = posX.x;
            }
            // Slide along Z
            XMFLOAT3 posZ = { m_position.x, m_position.y, targetPos.z };
            if (!CheckCollision(posZ)) {
                m_position.z = posZ.z;
            }
        }
    }
}

void Camera::MoveUp(float d) {
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // World up
    pos += up * d;
    XMStoreFloat3(&m_position, pos);
}

void Camera::AddPitch(float p) {
    m_pitch += p;
    // Clamp pitch to avoid flipping
    m_pitch = std::clamp(m_pitch, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
    UpdateVectors();
}

void Camera::AddYaw(float y) {
    m_yaw += y;
    UpdateVectors();
}

void Camera::Jump(float force) {
    // Cannot jump while crouching
    if (m_isGrounded && !m_isCrouching) {
        m_velocity.y = force;
        m_isGrounded = false;
    }
}

void Camera::SetCrouching(bool isCrouching) {
    m_isCrouching = isCrouching;
}

void Camera::SetControllerPose(const XMFLOAT3& eyePosition, bool grounded) {
    m_position = eyePosition;
    m_velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    m_isGrounded = grounded;
}

void Camera::Update(float dt, bool simulateMovement) {
    if (m_isFreecam) return; // Skip gravity, headbob, and floor collision

    // A PhysX-controlled camera follows the capsule; it must not fall a second time.
    if (simulateMovement) {
        m_velocity.y += m_gravity * dt;

        m_position.y += m_velocity.y * dt;

        // Floor collision
        float targetEyeHeight = m_isCrouching ? m_crouchHeight : m_standHeight;
        // Smooth eye height interpolation
        m_currentEyeHeight += (targetEyeHeight - m_currentEyeHeight) * 10.0f * dt;
        
        float minAllowedY = m_floorY + m_currentEyeHeight;
        if (m_floorEnabled && m_position.y <= minAllowedY) {
            m_position.y = minAllowedY;
            m_velocity.y = 0.0f;
            m_isGrounded = true;
        } else {
            m_isGrounded = false;
        }

        if (m_position.y < m_killPlaneY) {
            m_position = XMFLOAT3(0.0f, m_floorY + m_standHeight, -5.0f);
            m_velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
            m_isGrounded = true;
        }
    }
    
    // Update head bob variables
    float dx = m_position.x - m_lastPos.x;
    float dz = m_position.z - m_lastPos.z;
    float distMoved = sqrt(dx*dx + dz*dz);
    m_lastPos = m_position;

    if (m_isGrounded) {
        m_bobTime += distMoved;
        if (distMoved > 0.0001f) {
            m_bobMagnitude += (1.0f - m_bobMagnitude) * 10.0f * dt;
        } else {
            m_bobMagnitude += (0.0f - m_bobMagnitude) * 10.0f * dt;
        }
    } else {
        m_bobMagnitude += (0.0f - m_bobMagnitude) * 10.0f * dt;
    }
}

void Camera::UpdateVectors() {
    XMVECTOR forward = XMVectorSet(
        cosf(m_pitch) * sinf(m_yaw),
        sinf(m_pitch),
        cosf(m_pitch) * cosf(m_yaw),
        0.0f
    );

    forward = XMVector3Normalize(forward);
    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
    
    XMStoreFloat3(&m_forward, forward);
    XMStoreFloat3(&m_right, right);
    XMStoreFloat3(&m_up, up);
}

bool Camera::CheckCollision(const XMFLOAT3& targetPos) const {
    if (!m_collisionEnabled || m_collisionBoxes.empty()) {
        return false;
    }

    float radius = 0.35f;
    float playerFeetY = targetPos.y - m_currentEyeHeight;
    float playerHeadY = targetPos.y + 0.2f;

    for (const auto& box : m_collisionBoxes) {
        bool overlapX = (targetPos.x + radius > box.minPoint.x) && (targetPos.x - radius < box.maxPoint.x);
        bool overlapZ = (targetPos.z + radius > box.minPoint.z) && (targetPos.z - radius < box.maxPoint.z);
        bool overlapY = (playerHeadY > box.minPoint.y) && (playerFeetY < box.maxPoint.y);

        if (overlapX && overlapZ && overlapY) {
            return true; // Collision detected!
        }
    }

    return false; // Free to move
}

XMMATRIX Camera::GetViewMatrix() const {
    XMFLOAT3 bobPos = m_position;
    XMFLOAT3 bobForward = m_forward;
    XMFLOAT3 bobUp = m_up;
    XMFLOAT3 bobRight = m_right;
    
    XMVECTOR pos = XMLoadFloat3(&bobPos);
    XMVECTOR forward = XMLoadFloat3(&bobForward);
    XMVECTOR up = XMLoadFloat3(&bobUp);

    return XMMatrixLookToLH(pos, forward, up);
}

static float Halton(int index, int base) {
    float f = 1.0f;
    float r = 0.0f;
    int current = index;
    do {
        f = f / base;
        r = r + f * (current % base);
        current = current / base;
    } while (current > 0);
    return r;
}

void Camera::Jitter(int frameIndex, int width, int height) {
    // 8 phase jitter
    int phase = frameIndex % 8;
    
    // Halton sequence (base 2 for X, base 3 for Y)
    float x = Halton(phase + 1, 2) - 0.5f;
    float y = Halton(phase + 1, 3) - 0.5f;
    
    // Subpixel space offset
    m_jitterX = x * (2.0f / width);
    m_jitterY = y * (2.0f / height);
}

XMMATRIX Camera::GetProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane) const {
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspectRatio, nearPlane, farPlane);
    
    // Apply subpixel jitter for temporal upscaling (DLSS/TAA standard method)
    // Modify proj[2][0] and proj[2][1] to shift NDC before perspective divide.
    // This is equivalent to translating the image by (jitterX, jitterY) in NDC space.
    if (m_jitterX != 0.0f || m_jitterY != 0.0f) {
        proj.r[2] = XMVectorSetX(proj.r[2], XMVectorGetX(proj.r[2]) + m_jitterX);
        proj.r[2] = XMVectorSetY(proj.r[2], XMVectorGetY(proj.r[2]) + m_jitterY);
    }
    
    return proj;
}

XMMATRIX Camera::GetCleanProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane) const {
    return XMMatrixPerspectiveFovLH(fov, aspectRatio, nearPlane, farPlane);
}
