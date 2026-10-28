#include "MotionTrajectory.h"
#include <cmath>
#include <algorithm>

namespace EngineAnim {

namespace {
    inline float HalflifeToDamping(float halflife) {
        return (4.0f * 0.69314718056f) / (halflife + 1e-4f);
    }

    inline float FastNegExp(float x) {
        return 1.0f / (1.0f + x + 0.48f*x*x + 0.235f*x*x*x);
    }

    void SpringDamperPosition(
        Vec3& x,
        Vec3& v,
        Vec3& a,
        const Vec3& vGoal,
        float halflife,
        float dt
    ) {
        float y = HalflifeToDamping(halflife) / 2.0f;
        Vec3 j0 = v - vGoal;
        Vec3 j1 = a + j0 * y;
        float eydt = FastNegExp(y * dt);

        Vec3 xPrev = x;
        x = (j1 / (y*y) + j0 / y + vGoal * dt + xPrev) +
            eydt * ((-j1 / (y*y)) + ((-j0 - j1 * dt) / y));
        v = eydt * (j0 + j1 * dt) + vGoal;
        a = eydt * (a - j1 * y * dt);
    }

    Quat DesiredRotationFromDirection(const Vec3& dir, const Quat& currentRot) {
        if (dir.LengthSq() < 1e-4f) return currentRot;
        float angle = std::atan2(dir.x, dir.z);
        return Quat(0.0f, std::sin(angle * 0.5f), 0.0f, std::cos(angle * 0.5f));
    }
}

MotionTrajectory::MotionTrajectory() {
    m_points.resize(TRAJECTORY_POINTS);
}

void MotionTrajectory::Update(
    Vec3 currentPos,
    Quat currentRot,
    Vec3 currentVel,
    Vec3 inputDir,
    float maxSpeed,
    float halflife,
    float dt
) {
    m_position = currentPos;
    m_rotation = currentRot;

    Vec3 desiredVel = inputDir.Normalized() * (inputDir.Length() * maxSpeed);
    Quat desiredRot = DesiredRotationFromDirection(inputDir, currentRot);

    // Update current simulation point
    SpringDamperPosition(m_position, m_velocity, m_acceleration, desiredVel, halflife, dt);

    // Update rotation toward desired direction
    if (inputDir.LengthSq() > 1e-4f) {
        float rotDamping = HalflifeToDamping(halflife * 0.5f);
        float rotFactor = 1.0f - FastNegExp(rotDamping * dt);
        m_rotation = Slerp(m_rotation, desiredRot, rotFactor).Normalized();
    }

    // Predict future horizons: t = 0 (now), t = 0.33s (20f), t = 0.66s (40f), t = 1.0s (60f)
    m_points[0] = { m_position, m_velocity, m_rotation, m_angularVelocity };

    const float horizons[3] = { 0.333f, 0.666f, 1.0f };
    for (int i = 0; i < 3; ++i) {
        float h = horizons[i];
        Vec3 pSim = m_position;
        Vec3 vSim = m_velocity;
        Vec3 aSim = m_acceleration;

        SpringDamperPosition(pSim, vSim, aSim, desiredVel, halflife, h);

        Quat rSim = m_rotation;
        if (inputDir.LengthSq() > 1e-4f) {
            float rotDamping = HalflifeToDamping(halflife * 0.5f);
            float rotFactor = 1.0f - FastNegExp(rotDamping * h);
            rSim = Slerp(m_rotation, desiredRot, rotFactor).Normalized();
        }

        m_points[i + 1] = { pSim, vSim, rSim, Vec3(0, 0, 0) };
    }
}

} // namespace EngineAnim
