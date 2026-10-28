#pragma once
#include "../AnimationCommon.h"
#include <vector>

namespace EngineAnim {

struct TrajectoryPoint {
    Vec3 position;
    Vec3 velocity;
    Quat rotation;
    Vec3 angularVelocity;
};

class MotionTrajectory {
public:
    enum {
        TRAJECTORY_POINTS = 4 // t = 0 (current), t = 20 frames (0.33s), t = 40 frames (0.66s), t = 60 frames (1.0s)
    };

    MotionTrajectory();

    void Update(
        Vec3 currentPos,
        Quat currentRot,
        Vec3 currentVel,
        Vec3 inputDir, // In world space (derived from WASD + camera yaw)
        float maxSpeed,
        float halflife,
        float dt
    );

    const std::vector<TrajectoryPoint>& GetPoints() const { return m_points; }
    const TrajectoryPoint& GetPoint(int idx) const { return m_points[idx]; }

    Vec3 GetPosition() const { return m_position; }
    Quat GetRotation() const { return m_rotation; }
    Vec3 GetVelocity() const { return m_velocity; }

    void SetPosition(const Vec3& p) { m_position = p; }
    void SetRotation(const Quat& r) { m_rotation = r; }

private:
    Vec3 m_position = Vec3(0, 0, 0);
    Quat m_rotation = Quat::Identity();
    Vec3 m_velocity = Vec3(0, 0, 0);
    Vec3 m_acceleration = Vec3(0, 0, 0);
    Vec3 m_angularVelocity = Vec3(0, 0, 0);

    std::vector<TrajectoryPoint> m_points;
};

} // namespace EngineAnim
