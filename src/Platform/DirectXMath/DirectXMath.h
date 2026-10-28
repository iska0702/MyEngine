#pragma once

#if defined(_WIN32)
    // On Windows, use system DirectXMath from Windows SDK
    #include <directxmath.h>
#else

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

#if defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>
#endif

namespace DirectX {

constexpr float XM_PI = 3.14159265358979323846f;
constexpr float XM_2PI = 6.28318530717958647692f;
constexpr float XM_PIDIV2 = 1.57079632679489661923f;
constexpr float XM_PIDIV4 = 0.785398163397448309616f;

inline float XMConvertToDegrees(float fRadians) {
    return fRadians * (180.0f / XM_PI);
}

inline float XMConvertToRadians(float fDegrees) {
    return fDegrees * (XM_PI / 180.0f);
}

struct XMFLOAT2 {
    float x = 0.0f;
    float y = 0.0f;
    constexpr XMFLOAT2() = default;
    constexpr XMFLOAT2(float _x, float _y) : x(_x), y(_y) {}
};

struct XMFLOAT3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    constexpr XMFLOAT3() = default;
    constexpr XMFLOAT3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

struct XMFLOAT4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
    constexpr XMFLOAT4() = default;
    constexpr XMFLOAT4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct XMFLOAT4X4 {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
    XMFLOAT4X4() = default;
    constexpr XMFLOAT4X4(float m00, float m01, float m02, float m03,
                         float m10, float m11, float m12, float m13,
                         float m20, float m21, float m22, float m23,
                         float m30, float m31, float m32, float m33)
        : _11(m00), _12(m01), _13(m02), _14(m03),
          _21(m10), _22(m11), _23(m12), _24(m13),
          _31(m20), _32(m21), _33(m22), _34(m23),
          _41(m30), _42(m31), _43(m32), _44(m33) {}
};

#if defined(__ARM_NEON) || defined(__aarch64__)
typedef float32x4_t XMVECTOR;
#else
struct XMVECTOR {
    float v[4];
};
#endif

struct XMMATRIX {
    XMVECTOR r[4];

    XMMATRIX() = default;
    XMMATRIX(XMVECTOR r0, XMVECTOR r1, XMVECTOR r2, XMVECTOR r3) {
        r[0] = r0; r[1] = r1; r[2] = r2; r[3] = r3;
    }
};

inline XMVECTOR XMVectorSet(float x, float y, float z, float w) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    float data[4] = { x, y, z, w };
    return vld1q_f32(data);
#else
    XMVECTOR v; v.v[0] = x; v.v[1] = y; v.v[2] = z; v.v[3] = w; return v;
#endif
}

inline XMVECTOR XMVectorZero() {
    return XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
}

inline float XMVectorGetX(XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vgetq_lane_f32(V, 0);
#else
    return V.v[0];
#endif
}

inline float XMVectorGetY(XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vgetq_lane_f32(V, 1);
#else
    return V.v[1];
#endif
}

inline float XMVectorGetZ(XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vgetq_lane_f32(V, 2);
#else
    return V.v[2];
#endif
}

inline float XMVectorGetW(XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vgetq_lane_f32(V, 3);
#else
    return V.v[3];
#endif
}

inline XMVECTOR XMVectorReplicate(float Value) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vdupq_n_f32(Value);
#else
    return XMVectorSet(Value, Value, Value, Value);
#endif
}

inline XMVECTOR XMVectorSetX(XMVECTOR V, float x) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vsetq_lane_f32(x, V, 0);
#else
    V.v[0] = x; return V;
#endif
}

inline XMVECTOR XMVectorSetY(XMVECTOR V, float y) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vsetq_lane_f32(y, V, 1);
#else
    V.v[1] = y; return V;
#endif
}

inline XMVECTOR XMVectorSetZ(XMVECTOR V, float z) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vsetq_lane_f32(z, V, 2);
#else
    V.v[2] = z; return V;
#endif
}

inline XMVECTOR XMVectorSetW(XMVECTOR V, float w) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vsetq_lane_f32(w, V, 3);
#else
    V.v[3] = w; return V;
#endif
}

inline XMVECTOR XMVectorMultiplyAdd(XMVECTOR V1, XMVECTOR V2, XMVECTOR V3) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vmlaq_f32(V3, V1, V2);
#else
    return XMVectorSet(
        V1.v[0] * V2.v[0] + V3.v[0],
        V1.v[1] * V2.v[1] + V3.v[1],
        V1.v[2] * V2.v[2] + V3.v[2],
        V1.v[3] * V2.v[3] + V3.v[3]
    );
#endif
}

inline XMVECTOR XMLoadFloat3(const XMFLOAT3* pSource) {
    return XMVectorSet(pSource->x, pSource->y, pSource->z, 0.0f);
}

inline void XMStoreFloat3(XMFLOAT3* pDestination, XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    float data[4];
    vst1q_f32(data, V);
    pDestination->x = data[0];
    pDestination->y = data[1];
    pDestination->z = data[2];
#else
    pDestination->x = V.v[0];
    pDestination->y = V.v[1];
    pDestination->z = V.v[2];
#endif
}

inline XMVECTOR XMLoadFloat4(const XMFLOAT4* pSource) {
    return XMVectorSet(pSource->x, pSource->y, pSource->z, pSource->w);
}

inline void XMStoreFloat4(XMFLOAT4* pDestination, XMVECTOR V) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    vst1q_f32((float*)pDestination, V);
#else
    pDestination->x = V.v[0];
    pDestination->y = V.v[1];
    pDestination->z = V.v[2];
    pDestination->w = V.v[3];
#endif
}

inline XMMATRIX XMLoadFloat4x4(const XMFLOAT4X4* pSource) {
    XMMATRIX m;
    m.r[0] = XMLoadFloat4((const XMFLOAT4*)&pSource->m[0]);
    m.r[1] = XMLoadFloat4((const XMFLOAT4*)&pSource->m[1]);
    m.r[2] = XMLoadFloat4((const XMFLOAT4*)&pSource->m[2]);
    m.r[3] = XMLoadFloat4((const XMFLOAT4*)&pSource->m[3]);
    return m;
}

inline void XMStoreFloat4x4(XMFLOAT4X4* pDestination, const XMMATRIX& M) {
    XMStoreFloat4((XMFLOAT4*)&pDestination->m[0], M.r[0]);
    XMStoreFloat4((XMFLOAT4*)&pDestination->m[1], M.r[1]);
    XMStoreFloat4((XMFLOAT4*)&pDestination->m[2], M.r[2]);
    XMStoreFloat4((XMFLOAT4*)&pDestination->m[3], M.r[3]);
}

inline XMVECTOR XMVectorScale(XMVECTOR V, float ScaleFactor) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vmulq_n_f32(V, ScaleFactor);
#else
    return XMVectorSet(V.v[0] * ScaleFactor, V.v[1] * ScaleFactor, V.v[2] * ScaleFactor, V.v[3] * ScaleFactor);
#endif
}

inline XMVECTOR XMVectorAdd(XMVECTOR V1, XMVECTOR V2) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vaddq_f32(V1, V2);
#else
    return XMVectorSet(V1.v[0] + V2.v[0], V1.v[1] + V2.v[1], V1.v[2] + V2.v[2], V1.v[3] + V2.v[3]);
#endif
}

inline XMVECTOR XMVectorSubtract(XMVECTOR V1, XMVECTOR V2) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vsubq_f32(V1, V2);
#else
    return XMVectorSet(V1.v[0] - V2.v[0], V1.v[1] - V2.v[1], V1.v[2] - V2.v[2], V1.v[3] - V2.v[3]);
#endif
}

inline XMVECTOR XMVectorMultiply(XMVECTOR V1, XMVECTOR V2) {
#if defined(__ARM_NEON) || defined(__aarch64__)
    return vmulq_f32(V1, V2);
#else
    return XMVectorSet(V1.v[0] * V2.v[0], V1.v[1] * V2.v[1], V1.v[2] * V2.v[2], V1.v[3] * V2.v[3]);
#endif
}

#if !defined(__ARM_NEON) && !defined(__aarch64__)
inline XMVECTOR operator+(XMVECTOR V1, XMVECTOR V2) { return XMVectorAdd(V1, V2); }
inline XMVECTOR operator-(XMVECTOR V1, XMVECTOR V2) { return XMVectorSubtract(V1, V2); }
inline XMVECTOR operator*(XMVECTOR V1, XMVECTOR V2) { return XMVectorMultiply(V1, V2); }
inline XMVECTOR operator*(XMVECTOR V, float S) { return XMVectorScale(V, S); }
inline XMVECTOR operator*(float S, XMVECTOR V) { return XMVectorScale(V, S); }
inline XMVECTOR& operator+=(XMVECTOR& V1, XMVECTOR V2) { V1 = XMVectorAdd(V1, V2); return V1; }
inline XMVECTOR& operator-=(XMVECTOR& V1, XMVECTOR V2) { V1 = XMVectorSubtract(V1, V2); return V1; }
inline XMVECTOR& operator*=(XMVECTOR& V, float S) { V = XMVectorScale(V, S); return V; }
#endif

inline XMVECTOR XMVector3Dot(XMVECTOR V1, XMVECTOR V2) {
    XMFLOAT4 a, b;
    XMStoreFloat4(&a, V1);
    XMStoreFloat4(&b, V2);
    float d = a.x * b.x + a.y * b.y + a.z * b.z;
    return XMVectorSet(d, d, d, d);
}

inline XMVECTOR XMVector3LengthSq(XMVECTOR V) {
    return XMVector3Dot(V, V);
}

inline XMVECTOR XMVector3Length(XMVECTOR V) {
    XMFLOAT4 a;
    XMStoreFloat4(&a, V);
    float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return XMVectorSet(l, l, l, l);
}

inline XMVECTOR XMVector3Normalize(XMVECTOR V) {
    XMFLOAT4 a;
    XMStoreFloat4(&a, V);
    float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (l > 1e-6f) {
        float inv = 1.0f / l;
        return XMVectorSet(a.x * inv, a.y * inv, a.z * inv, 0.0f);
    }
    return XMVectorZero();
}

inline XMVECTOR XMVector3Cross(XMVECTOR V1, XMVECTOR V2) {
    XMFLOAT4 a, b;
    XMStoreFloat4(&a, V1);
    XMStoreFloat4(&b, V2);
    return XMVectorSet(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
        0.0f
    );
}

inline XMMATRIX XMMatrixIdentity() {
    return XMMATRIX(
        XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f)
    );
}

inline XMMATRIX XMMatrixScaling(float ScaleX, float ScaleY, float ScaleZ) {
    return XMMATRIX(
        XMVectorSet(ScaleX, 0.0f,   0.0f,   0.0f),
        XMVectorSet(0.0f,   ScaleY, 0.0f,   0.0f),
        XMVectorSet(0.0f,   0.0f,   ScaleZ, 0.0f),
        XMVectorSet(0.0f,   0.0f,   0.0f,   1.0f)
    );
}

inline XMMATRIX XMMatrixTranslation(float OffsetX, float OffsetY, float OffsetZ) {
    return XMMATRIX(
        XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f),
        XMVectorSet(OffsetX, OffsetY, OffsetZ, 1.0f)
    );
}

inline XMMATRIX XMMatrixTranslationFromVector(XMVECTOR Offset) {
    return XMMatrixTranslation(XMVectorGetX(Offset), XMVectorGetY(Offset), XMVectorGetZ(Offset));
}

inline XMMATRIX XMMatrixRotationY(float Angle) {
    float s = std::sin(Angle);
    float c = std::cos(Angle);
    return XMMATRIX(
        XMVectorSet(c,    0.0f, -s,   0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        XMVectorSet(s,    0.0f, c,    0.0f),
        XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f)
    );
}

inline XMVECTOR XMVectorLerp(XMVECTOR V0, XMVECTOR V1, float t) {
    return V0 + (V1 - V0) * t;
}

inline XMMATRIX XMMatrixRotationRollPitchYaw(float Pitch, float Yaw, float Roll) {
    float cp = std::cos(Pitch), sp = std::sin(Pitch);
    float cy = std::cos(Yaw),   sy = std::sin(Yaw);
    float cr = std::cos(Roll),  sr = std::sin(Roll);

    XMMATRIX m;
    m.r[0] = XMVectorSet(cr * cy + sr * sp * sy, sr * cp, cr * -sy + sr * sp * cy, 0.0f);
    m.r[1] = XMVectorSet(-sr * cy + cr * sp * sy, cr * cp, sr * sy + cr * sp * cy, 0.0f);
    m.r[2] = XMVectorSet(cp * sy, -sp, cp * cy, 0.0f);
    m.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return m;
}

inline XMMATRIX XMMatrixRotationQuaternion(XMVECTOR Quaternion) {
    float x = XMVectorGetX(Quaternion);
    float y = XMVectorGetY(Quaternion);
    float z = XMVectorGetZ(Quaternion);
    float w = XMVectorGetW(Quaternion);

    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;

    XMMATRIX m;
    m.r[0] = XMVectorSet(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f);
    m.r[1] = XMVectorSet(2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f);
    m.r[2] = XMVectorSet(2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f);
    m.r[3] = XMVectorSet(0.0f,                    0.0f,                    0.0f,                    1.0f);
    return m;
}

inline XMMATRIX XMMatrixSet(
    float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23,
    float m30, float m31, float m32, float m33) {
    XMMATRIX m;
    m.r[0] = XMVectorSet(m00, m01, m02, m03);
    m.r[1] = XMVectorSet(m10, m11, m12, m13);
    m.r[2] = XMVectorSet(m20, m21, m22, m23);
    m.r[3] = XMVectorSet(m30, m31, m32, m33);
    return m;
}

inline XMVECTOR XMQuaternionRotationRollPitchYaw(float Pitch, float Yaw, float Roll) {
    float p = Pitch * 0.5f;
    float y = Yaw * 0.5f;
    float r = Roll * 0.5f;

    float sinP = std::sin(p), cosP = std::cos(p);
    float sinY = std::sin(y), cosY = std::cos(y);
    float sinR = std::sin(r), cosR = std::cos(r);

    return XMVectorSet(
        sinP * cosY * cosR + cosP * sinY * sinR,
        cosP * sinY * cosR - sinP * cosY * sinR,
        cosP * cosY * sinR - sinP * sinY * cosR,
        cosP * cosY * cosR + sinP * sinY * sinR
    );
}

inline XMMATRIX XMMatrixMultiply(const XMMATRIX& M1, const XMMATRIX& M2) {
    XMFLOAT4X4 a, b, out;
    XMStoreFloat4x4(&a, M1);
    XMStoreFloat4x4(&b, M2);

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out.m[i][j] = a.m[i][0] * b.m[0][j] +
                          a.m[i][1] * b.m[1][j] +
                          a.m[i][2] * b.m[2][j] +
                          a.m[i][3] * b.m[3][j];
        }
    }
    return XMLoadFloat4x4(&out);
}

inline XMMATRIX operator*(const XMMATRIX& M1, const XMMATRIX& M2) {
    return XMMatrixMultiply(M1, M2);
}

inline XMVECTOR XMVector3Transform(XMVECTOR V, const XMMATRIX& M) {
    XMFLOAT4 v;
    XMStoreFloat4(&v, V);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, M);

    return XMVectorSet(
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2],
        v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3]
    );
}

inline XMVECTOR XMVector4Transform(XMVECTOR V, const XMMATRIX& M) {
    XMFLOAT4 v;
    XMStoreFloat4(&v, V);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, M);

    return XMVectorSet(
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2],
        v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3]
    );
}

inline XMVECTOR XMVector3TransformCoord(XMVECTOR V, const XMMATRIX& M) {
    XMFLOAT4 v;
    XMStoreFloat4(&v, V);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, M);

    float norm = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
    float invNorm = (std::abs(norm) > 1e-6f) ? (1.0f / norm) : 1.0f;

    return XMVectorSet(
        (v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0]) * invNorm,
        (v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1]) * invNorm,
        (v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2]) * invNorm,
        1.0f
    );
}

inline XMVECTOR XMVector3TransformNormal(XMVECTOR V, const XMMATRIX& M) {
    XMFLOAT4 v;
    XMStoreFloat4(&v, V);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, M);

    return XMVectorSet(
        v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2],
        0.0f
    );
}

inline XMMATRIX XMMatrixTranspose(const XMMATRIX& M) {
    XMFLOAT4X4 m, out;
    XMStoreFloat4x4(&m, M);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out.m[i][j] = m.m[j][i];
    return XMLoadFloat4x4(&out);
}

inline XMMATRIX XMMatrixInverse(XMVECTOR* pDeterminant, const XMMATRIX& M) {
    XMFLOAT4X4 m, inv;
    XMStoreFloat4x4(&m, M);

    float a0 = m.m[0][0] * m.m[1][1] - m.m[0][1] * m.m[1][0];
    float a1 = m.m[0][0] * m.m[1][2] - m.m[0][2] * m.m[1][0];
    float a2 = m.m[0][0] * m.m[1][3] - m.m[0][3] * m.m[1][0];
    float a3 = m.m[0][1] * m.m[1][2] - m.m[0][2] * m.m[1][1];
    float a4 = m.m[0][1] * m.m[1][3] - m.m[0][3] * m.m[1][1];
    float a5 = m.m[0][2] * m.m[1][3] - m.m[0][3] * m.m[1][2];
    float b0 = m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0];
    float b1 = m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0];
    float b2 = m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0];
    float b3 = m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1];
    float b4 = m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1];
    float b5 = m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2];

    float det = a0 * b5 - a1 * b4 + a2 * b3 + a3 * b2 - a4 * b1 + a5 * b0;
    if (pDeterminant) *pDeterminant = XMVectorSet(det, det, det, det);
    if (std::abs(det) < 1e-8f) return XMMatrixIdentity();

    float invDet = 1.0f / det;

    inv.m[0][0] = ( m.m[1][1] * b5 - m.m[1][2] * b4 + m.m[1][3] * b3) * invDet;
    inv.m[0][1] = (-m.m[0][1] * b5 + m.m[0][2] * b4 - m.m[0][3] * b3) * invDet;
    inv.m[0][2] = ( m.m[3][1] * a5 - m.m[3][2] * a4 + m.m[3][3] * a3) * invDet;
    inv.m[0][3] = (-m.m[2][1] * a5 + m.m[2][2] * a4 - m.m[2][3] * a3) * invDet;

    inv.m[1][0] = (-m.m[1][0] * b5 + m.m[1][2] * b2 - m.m[1][3] * b1) * invDet;
    inv.m[1][1] = ( m.m[0][0] * b5 - m.m[0][2] * b2 + m.m[0][3] * b1) * invDet;
    inv.m[1][2] = (-m.m[3][0] * a5 + m.m[3][2] * a2 - m.m[3][3] * a1) * invDet;
    inv.m[1][3] = ( m.m[2][0] * a5 - m.m[2][2] * a2 + m.m[2][3] * a1) * invDet;

    inv.m[2][0] = ( m.m[1][0] * b4 - m.m[1][1] * b2 + m.m[1][3] * b0) * invDet;
    inv.m[2][1] = (-m.m[0][0] * b4 + m.m[0][1] * b2 - m.m[0][3] * b0) * invDet;
    inv.m[2][2] = ( m.m[3][0] * a4 - m.m[3][1] * a2 + m.m[3][3] * a0) * invDet;
    inv.m[2][3] = (-m.m[2][0] * a4 + m.m[2][1] * a2 - m.m[2][3] * a0) * invDet;

    inv.m[3][0] = (-m.m[1][0] * b3 + m.m[1][1] * b1 - m.m[1][2] * b0) * invDet;
    inv.m[3][1] = ( m.m[0][0] * b3 - m.m[0][1] * b1 + m.m[0][2] * b0) * invDet;
    inv.m[3][2] = (-m.m[3][0] * a3 + m.m[3][1] * a1 - m.m[3][2] * a0) * invDet;
    inv.m[3][3] = ( m.m[2][0] * a3 - m.m[2][1] * a1 + m.m[2][2] * a0) * invDet;

    return XMLoadFloat4x4(&inv);
}

inline XMMATRIX XMMatrixPerspectiveFovLH(float FovAngleY, float AspectRatio, float NearZ, float FarZ) {
    float SinFov = std::sin(0.5f * FovAngleY);
    float CosFov = std::cos(0.5f * FovAngleY);
    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;
    float fRange = FarZ / (FarZ - NearZ);

    XMMATRIX m;
    m.r[0] = XMVectorSet(Width, 0.0f,   0.0f,            0.0f);
    m.r[1] = XMVectorSet(0.0f,  Height, 0.0f,            0.0f);
    m.r[2] = XMVectorSet(0.0f,  0.0f,   fRange,          1.0f);
    m.r[3] = XMVectorSet(0.0f,  0.0f,   -fRange * NearZ, 0.0f);
    return m;
}

inline XMMATRIX XMMatrixLookAtLH(XMVECTOR EyePosition, XMVECTOR FocusPosition, XMVECTOR UpDirection) {
    XMVECTOR R2 = XMVector3Normalize(XMVectorSubtract(FocusPosition, EyePosition));
    XMVECTOR R0 = XMVector3Normalize(XMVector3Cross(UpDirection, R2));
    XMVECTOR R1 = XMVector3Cross(R2, R0);

    XMVECTOR NegEyePosition = XMVectorScale(EyePosition, -1.0f);
    XMVECTOR D0 = XMVector3Dot(R0, NegEyePosition);
    XMVECTOR D1 = XMVector3Dot(R1, NegEyePosition);
    XMVECTOR D2 = XMVector3Dot(R2, NegEyePosition);

    XMFLOAT4 r0, r1, r2, d0, d1, d2;
    XMStoreFloat4(&r0, R0);
    XMStoreFloat4(&r1, R1);
    XMStoreFloat4(&r2, R2);
    XMStoreFloat4(&d0, D0);
    XMStoreFloat4(&d1, D1);
    XMStoreFloat4(&d2, D2);

    XMMATRIX m;
    m.r[0] = XMVectorSet(r0.x, r1.x, r2.x, 0.0f);
    m.r[1] = XMVectorSet(r0.y, r1.y, r2.y, 0.0f);
    m.r[2] = XMVectorSet(r0.z, r1.z, r2.z, 0.0f);
    m.r[3] = XMVectorSet(d0.x, d1.x, d2.x, 1.0f);
    return m;
}

inline XMMATRIX XMMatrixLookToLH(XMVECTOR EyePosition, XMVECTOR EyeDirection, XMVECTOR UpDirection) {
    XMVECTOR R2 = XMVector3Normalize(EyeDirection);
    XMVECTOR R0 = XMVector3Normalize(XMVector3Cross(UpDirection, R2));
    XMVECTOR R1 = XMVector3Cross(R2, R0);

    XMVECTOR NegEyePosition = XMVectorScale(EyePosition, -1.0f);
    XMVECTOR D0 = XMVector3Dot(R0, NegEyePosition);
    XMVECTOR D1 = XMVector3Dot(R1, NegEyePosition);
    XMVECTOR D2 = XMVector3Dot(R2, NegEyePosition);

    XMFLOAT4 r0, r1, r2, d0, d1, d2;
    XMStoreFloat4(&r0, R0);
    XMStoreFloat4(&r1, R1);
    XMStoreFloat4(&r2, R2);
    XMStoreFloat4(&d0, D0);
    XMStoreFloat4(&d1, D1);
    XMStoreFloat4(&d2, D2);

    XMMATRIX m;
    m.r[0] = XMVectorSet(r0.x, r1.x, r2.x, 0.0f);
    m.r[1] = XMVectorSet(r0.y, r1.y, r2.y, 0.0f);
    m.r[2] = XMVectorSet(r0.z, r1.z, r2.z, 0.0f);
    m.r[3] = XMVectorSet(d0.x, d1.x, d2.x, 1.0f);
    return m;
}

} // namespace DirectX

#endif
