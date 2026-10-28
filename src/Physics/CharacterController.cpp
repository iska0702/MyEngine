#include "CharacterController.h"
#if defined(_WIN32)
#include "imgui/imgui.h"

#include <PxPhysicsAPI.h>
#include <characterkinematic/PxControllerManager.h>
#include <characterkinematic/PxCapsuleController.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace EnginePhysics {

class CharacterController::HitReport : public physx::PxUserControllerHitReport {
public:
    float pushForce = 35.0f;

    void onShapeHit(const physx::PxControllerShapeHit& hit) override {
        physx::PxRigidActor* actor = hit.shape->getActor();
        if (!actor) return;

        physx::PxRigidDynamic* dynamic = actor->is<physx::PxRigidDynamic>();
        if (dynamic && !(dynamic->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)) {
            physx::PxVec3 pushDir = hit.dir;
            pushDir.y = 0.0f; // Horizontal push impulse
            if (pushDir.magnitudeSquared() > 0.001f) {
                pushDir.normalize();
                dynamic->addForce(pushDir * pushForce, physx::PxForceMode::eIMPULSE);
            }
        }
    }

    void onControllerHit(const physx::PxControllersHit& /*hit*/) override {}
    void onObstacleHit(const physx::PxControllerObstacleHit& /*hit*/) override {}
};

CharacterController& CharacterController::Get() {
    static CharacterController s_instance;
    return s_instance;
}

CharacterController::CharacterController() = default;

CharacterController::~CharacterController() {
    Shutdown();
}

bool CharacterController::Initialize(physx::PxPhysics* physics, physx::PxScene* scene, physx::PxMaterial* material) {
    if (m_controller != nullptr) return true;
    if (!physics || !scene || !material) return false;

    m_physics = physics;
    m_scene = scene;
    m_material = material;

    m_hitReport = std::make_unique<HitReport>();
    m_hitReport->pushForce = m_config.pushForce;

    m_manager = PxCreateControllerManager(*scene);
    if (!m_manager) {
        std::cerr << "[CharacterController] Failed to create PxControllerManager!" << std::endl;
        return false;
    }

    physx::PxCapsuleControllerDesc desc;
    desc.radius = m_config.radius;
    desc.height = m_config.height;
    desc.position = physx::PxExtendedVec3(0.0, 1.5, 0.0);
    desc.upDirection = physx::PxVec3(0, 1, 0);
    desc.slopeLimit = std::cos(DirectX::XMConvertToRadians(m_config.slopeLimitDegrees));
    desc.stepOffset = m_config.stepOffset;
    desc.contactOffset = 0.05f;
    desc.material = material;
    desc.reportCallback = m_hitReport.get();
    desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
    desc.climbingMode = physx::PxCapsuleClimbingMode::eEASY;

    m_controller = m_manager->createController(desc);
    if (!m_controller) {
        std::cerr << "[CharacterController] Failed to create PxCapsuleController!" << std::endl;
        return false;
    }
    m_capsule = static_cast<physx::PxCapsuleController*>(m_controller);

    std::cout << "[CharacterController] PhysX Kinematic Capsule Controller initialized successfully." << std::endl;
    return true;
}

void CharacterController::Shutdown() {
    if (m_controller) {
        m_controller->release();
        m_controller = nullptr;
        m_capsule = nullptr;
    }
    if (m_manager) {
        m_manager->release();
        m_manager = nullptr;
    }
    m_hitReport.reset();
}

void CharacterController::SetPosition(float x, float y, float z) {
    if (m_controller) {
        m_controller->setPosition(physx::PxExtendedVec3(x, y, z));
        m_verticalVelocity = 0.0f;
    }
}

DirectX::XMFLOAT3 CharacterController::GetPosition() const {
    if (!m_controller) return { 0.0f, 0.0f, 0.0f };
    physx::PxExtendedVec3 pos = m_controller->getPosition();
    return { static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z) };
}

DirectX::XMFLOAT3 CharacterController::GetEyePosition() const {
    DirectX::XMFLOAT3 pos = GetPosition();
    float height = m_isCrouching ? m_config.height * 0.5f : m_config.height;
    float eyeOffset = height * 0.5f + m_config.radius - 0.15f;
    return { pos.x, pos.y + eyeOffset, pos.z };
}

float CharacterController::GetCurrentSpeed() const {
    float vx = m_velocity.x;
    float vz = m_velocity.z;
    return std::sqrt(vx * vx + vz * vz);
}

void CharacterController::ApplyConfig() {
    if (!m_controller) return;

    if (m_hitReport) {
        m_hitReport->pushForce = m_config.pushForce;
    }

    m_controller->setStepOffset(m_config.stepOffset);
    m_controller->setSlopeLimit(std::cos(DirectX::XMConvertToRadians(m_config.slopeLimitDegrees)));

    if (m_capsule) {
        m_capsule->setRadius(m_config.radius);
        float h = m_isCrouching ? (m_config.height * 0.5f) : m_config.height;
        m_capsule->resize(h);
    }
}

void CharacterController::Update(float dt, const DirectX::XMFLOAT3& moveDir, bool wantJump, bool isRunning, bool isCrouching) {
    if (!m_enabled || !m_controller || dt <= 0.0f) return;

    // Handle Crouch height
    if (isCrouching != m_isCrouching) {
        m_isCrouching = isCrouching;
        if (m_capsule) {
            float targetHeight = m_isCrouching ? (m_config.height * 0.5f) : m_config.height;
            // PhysX resize preserves the foot position; setHeight moves the feet.
            m_capsule->resize(targetHeight);
        }
    }

    // Determine target speed
    float targetSpeed = m_config.walkSpeed;
    if (m_isCrouching) {
        targetSpeed = m_config.crouchSpeed;
    } else if (isRunning) {
        targetSpeed = m_config.runSpeed;
    }

    // Horizontal acceleration & friction
    float targetVx = moveDir.x * targetSpeed;
    float targetVz = moveDir.z * targetSpeed;

    float accelRate = m_isGrounded ? 16.0f : 4.0f; // Air control vs ground responsiveness
    m_velocity.x += (targetVx - m_velocity.x) * (std::min)(accelRate * dt, 1.0f);
    m_velocity.z += (targetVz - m_velocity.z) * (std::min)(accelRate * dt, 1.0f);

    // Jump Buffering & Coyote Time
    if (wantJump) {
        m_jumpBuffer = 0.15f;
    } else if (m_jumpBuffer > 0.0f) {
        m_jumpBuffer -= dt;
    }

    if (m_isGrounded) {
        m_coyoteTime = 0.12f;
        m_verticalVelocity = -2.5f; // Small ground snap force to smoothly descend slopes/steps
    } else {
        if (m_coyoteTime > 0.0f) {
            m_coyoteTime -= dt;
        }
        m_verticalVelocity += m_config.gravity * dt;
        m_verticalVelocity = (std::max)(m_verticalVelocity, -40.0f); // Terminal velocity
    }

    // Execute Jump
    if (m_jumpBuffer > 0.0f && m_coyoteTime > 0.0f) {
        m_verticalVelocity = m_config.jumpForce;
        m_isGrounded = false;
        m_coyoteTime = 0.0f;
        m_jumpBuffer = 0.0f;
    }

    // Compute final displacement vector
    physx::PxVec3 disp(
        m_velocity.x * dt,
        m_verticalVelocity * dt,
        m_velocity.z * dt
    );

    // Execute PhysX movement
    physx::PxControllerFilters filters;
    physx::PxControllerCollisionFlags collisionFlags = m_controller->move(disp, 0.001f, dt, filters);

    m_isGrounded = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);
    m_touchingCeiling = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_UP);
    m_touchingWall = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_SIDES);

    if (m_touchingCeiling && m_verticalVelocity > 0.0f) {
        m_verticalVelocity = 0.0f; // Cancel upward velocity on head bump
    }

    m_velocity.y = m_verticalVelocity;
}

void CharacterController::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    ImGui::SetNextWindowSize(ImVec2(480, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("PhysX Character Controller", nullptr)) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "Kinematic Capsule Controller (CCT)");
        ImGui::Text("Status: %s | Grounded: %s | Touching Wall: %s",
                    m_enabled ? "Enabled" : "Disabled (Noclip)",
                    m_isGrounded ? "YES" : "NO (In Air)",
                    m_touchingWall ? "YES" : "NO");

        DirectX::XMFLOAT3 pos = GetPosition();
        ImGui::Text("Position: X=%.2f, Y=%.2f, Z=%.2f", pos.x, pos.y, pos.z);
        ImGui::Text("Current Speed: %.2f m/s (Vert: %.2f m/s)", GetCurrentSpeed(), m_verticalVelocity);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Locomotion Settings:");

        bool changed = false;
        changed |= ImGui::SliderFloat("Walk Speed (m/s)", &m_config.walkSpeed, 1.0f, 20.0f, "%.1f");
        changed |= ImGui::SliderFloat("Sprint Speed (m/s)", &m_config.runSpeed, 2.0f, 35.0f, "%.1f");
        changed |= ImGui::SliderFloat("Crouch Speed (m/s)", &m_config.crouchSpeed, 0.5f, 10.0f, "%.1f");
        changed |= ImGui::SliderFloat("Jump Force", &m_config.jumpForce, 3.0f, 25.0f, "%.1f");
        changed |= ImGui::SliderFloat("Gravity", &m_config.gravity, -50.0f, -1.0f, "%.1f");
        changed |= ImGui::SliderFloat("Dynamic Push Force", &m_config.pushForce, 5.0f, 150.0f, "%.1f");

        ImGui::Spacing();
        ImGui::SeparatorText("Collision & Geometry Dimensions");
        changed |= ImGui::SliderFloat("Capsule Radius (m)", &m_config.radius, 0.15f, 1.2f, "%.2f");
        changed |= ImGui::SliderFloat("Capsule Height (m)", &m_config.height, 0.5f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Step Offset (m)", &m_config.stepOffset, 0.05f, 0.8f, "%.2f");
        changed |= ImGui::SliderFloat("Slope Limit (deg)", &m_config.slopeLimitDegrees, 15.0f, 75.0f, "%.1f");

        if (changed) {
            ApplyConfig();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Quick Actions");
        if (ImGui::Button("Teleport to Spawn (0, 2, 0)")) {
            SetPosition(0.0f, 2.0f, 0.0f);
        }
    }
    ImGui::End();
#endif
}

} // namespace EnginePhysics

#else // !defined(_WIN32)

namespace EnginePhysics {

CharacterController& CharacterController::Get() {
    static CharacterController s_instance;
    return s_instance;
}

class CharacterController::HitReport {};

CharacterController::CharacterController() = default;
CharacterController::~CharacterController() = default;

bool CharacterController::Initialize(physx::PxPhysics*, physx::PxScene*, physx::PxMaterial*) {
    return true;
}

void CharacterController::Shutdown() {}

void CharacterController::SetPosition(float x, float y, float z) {
    m_velocity = { 0.0f, 0.0f, 0.0f };
}

DirectX::XMFLOAT3 CharacterController::GetPosition() const {
    return { 0.0f, 0.0f, 0.0f };
}

DirectX::XMFLOAT3 CharacterController::GetEyePosition() const {
    return { 0.0f, 1.7f, 0.0f };
}

void CharacterController::Update(float, const DirectX::XMFLOAT3&, bool, bool, bool) {}

void CharacterController::ApplyConfig() {}

float CharacterController::GetCurrentSpeed() const {
    return 0.0f;
}

void CharacterController::DrawUI() {}

} // namespace EnginePhysics

#endif // defined(_WIN32)
