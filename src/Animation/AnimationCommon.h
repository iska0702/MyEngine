#pragma once
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace EngineAnim {

using namespace DirectX;

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float _x, float _y) : x(_x), y(_y) {}
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    Vec3(const XMFLOAT3& v) : x(v.x), y(v.y), z(v.z) {}

    XMFLOAT3 ToXM() const { return XMFLOAT3(x, y, z); }
    XMVECTOR ToVec() const { return XMVectorSet(x, y, z, 0.0f); }

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    float LengthSq() const { return x*x + y*y + z*z; }
    float Length() const { return std::sqrt(LengthSq()); }
    Vec3 Normalized() const {
        float l = Length();
        return l > 1e-6f ? (*this) * (1.0f / l) : Vec3(0, 0, 0);
    }
};

inline Vec3 operator*(float s, const Vec3& v) {
    return Vec3(v.x * s, v.y * s, v.z * s);
}

inline float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

inline Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
    return a + (b - a) * t;
}

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    Quat() = default;
    Quat(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    Quat(const XMFLOAT4& q) : x(q.x), y(q.y), z(q.z), w(q.w) {}

    XMFLOAT4 ToXM() const { return XMFLOAT4(x, y, z, w); }
    XMVECTOR ToVec() const { return XMVectorSet(x, y, z, w); }

    static Quat Identity() { return Quat(0, 0, 0, 1); }

    Quat operator*(const Quat& o) const {
        return Quat(
            w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x,
            w * o.z + x * o.y - y * o.x + z * o.w,
            w * o.w - x * o.x - y * o.y - z * o.z
        );
    }

    Quat Inverse() const {
        float n = x*x + y*y + z*z + w*w;
        if (n > 0.0f) {
            float invN = 1.0f / n;
            return Quat(-x * invN, -y * invN, -z * invN, w * invN);
        }
        return Identity();
    }

    Quat Normalized() const {
        float l = std::sqrt(x*x + y*y + z*z + w*w);
        if (l > 1e-6f) {
            float inv = 1.0f / l;
            return Quat(x * inv, y * inv, z * inv, w * inv);
        }
        return Identity();
    }
};

inline Vec3 Rotate(const Quat& q, const Vec3& v) {
    Vec3 qv(q.x, q.y, q.z);
    Vec3 uv = Cross(qv, v);
    Vec3 uuv = Cross(qv, uv);
    return v + (uv * (2.0f * q.w)) + (uuv * 2.0f);
}

inline Quat Slerp(const Quat& a, const Quat& b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat b_adj = b;
    if (dot < 0.0f) {
        dot = -dot;
        b_adj.x = -b.x; b_adj.y = -b.y; b_adj.z = -b.z; b_adj.w = -b.w;
    }
    if (dot > 0.9995f) {
        return Quat(
            a.x + (b_adj.x - a.x) * t,
            a.y + (b_adj.y - a.y) * t,
            a.z + (b_adj.z - a.z) * t,
            a.w + (b_adj.w - a.w) * t
        ).Normalized();
    }
    float theta_0 = std::acos(dot);
    float theta = theta_0 * t;
    float sin_theta = std::sin(theta);
    float sin_theta_0 = std::sin(theta_0);
    float s0 = std::cos(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;
    return Quat(
        a.x * s0 + b_adj.x * s1,
        a.y * s0 + b_adj.y * s1,
        a.z * s0 + b_adj.z * s1,
        a.w * s0 + b_adj.w * s1
    );
}

struct Transform {
    Vec3 translation = Vec3(0, 0, 0);
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3(1, 1, 1);

    Transform() = default;
    Transform(const Vec3& t, const Quat& r, const Vec3& s)
        : translation(t), rotation(r), scale(s) {}

    static Transform Identity() {
        return Transform(Vec3(0, 0, 0), Quat::Identity(), Vec3(1, 1, 1));
    }

    XMMATRIX ToMatrix() const {
        XMMATRIX s = XMMatrixScaling(scale.x, scale.y, scale.z);
        XMMATRIX r = XMMatrixRotationQuaternion(rotation.ToVec());
        XMMATRIX t = XMMatrixTranslation(translation.x, translation.y, translation.z);
        return s * r * t;
    }

    static Transform Interpolate(const Transform& a, const Transform& b, float t) {
        return Transform(
            Lerp(a.translation, b.translation, t),
            Slerp(a.rotation, b.rotation, t),
            Lerp(a.scale, b.scale, t)
        );
    }
};

struct Matrix4x4 {
    float m[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1}
    };

    Matrix4x4() = default;
    Matrix4x4(const XMMATRIX& mat) {
        XMFLOAT4X4 f;
        XMStoreFloat4x4(&f, mat);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                m[r][c] = f.m[r][c];
    }

    XMMATRIX ToXM() const {
        return XMMatrixSet(
            m[0][0], m[0][1], m[0][2], m[0][3],
            m[1][0], m[1][1], m[1][2], m[1][3],
            m[2][0], m[2][1], m[2][2], m[2][3],
            m[3][0], m[3][1], m[3][2], m[3][3]
        );
    }

    static Matrix4x4 Multiply(const Matrix4x4& a, const Matrix4x4& b) {
        Matrix4x4 r;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[i][j] = 0;
                for (int k = 0; k < 4; k++) {
                    r.m[i][j] += a.m[i][k] * b.m[k][j];
                }
            }
        }
        return r;
    }
};

} // namespace EngineAnim
