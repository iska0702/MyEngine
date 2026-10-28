#pragma once

#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "../../Platform/DirectXMath/DirectXMath.h"
#endif

#include <algorithm>
#include <cmath>

namespace EngineScene {

using namespace DirectX;

struct Transform {
    XMFLOAT3 localPosition = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 localRotation = { 0.0f, 0.0f, 0.0f }; // Euler angles in degrees (Pitch, Yaw, Roll)
    XMFLOAT3 localScale    = { 1.0f, 1.0f, 1.0f };

    XMFLOAT4X4 localMatrix;
    XMFLOAT4X4 worldMatrix;
    XMFLOAT4X4 prevWorldMatrix;

    bool isDirty = true;

    Transform() {
        XMMATRIX identity = XMMatrixIdentity();
        XMStoreFloat4x4(&localMatrix, identity);
        XMStoreFloat4x4(&worldMatrix, identity);
        XMStoreFloat4x4(&prevWorldMatrix, identity);
    }

    static bool Decompose(const XMMATRIX& matrix,XMFLOAT3& position,XMFLOAT3& rotation,XMFLOAT3& scale){
        XMVECTOR sv,q,p;if(!XMMatrixDecompose(&sv,&q,&p,matrix))return false;XMStoreFloat3(&position,p);XMStoreFloat3(&scale,sv);
        XMFLOAT4X4 m;XMStoreFloat4x4(&m,XMMatrixRotationQuaternion(q));float pitch=std::asin((std::clamp)(-m._32,-1.f,1.f));
        float yaw,roll;if(std::abs(std::cos(pitch))>.0001f){yaw=std::atan2(m._31,m._33);roll=std::atan2(m._12,m._22);}else{yaw=std::atan2(-m._13,m._11);roll=0;}
        rotation={XMConvertToDegrees(pitch),XMConvertToDegrees(yaw),XMConvertToDegrees(roll)};return true;
    }
    XMMATRIX ComputeLocalMatrix() {
        XMMATRIX S = XMMatrixScaling(localScale.x, localScale.y, localScale.z);
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(localRotation.x),
            XMConvertToRadians(localRotation.y),
            XMConvertToRadians(localRotation.z)
        );
        XMMATRIX T = XMMatrixTranslation(localPosition.x, localPosition.y, localPosition.z);
        XMMATRIX L = S * R * T;
        XMStoreFloat4x4(&localMatrix, L);
        return L;
    }

    void Update(const XMMATRIX& parentWorldMatrix) {
        prevWorldMatrix = worldMatrix;
        XMMATRIX L = ComputeLocalMatrix();
        XMMATRIX W = L * parentWorldMatrix;
        XMStoreFloat4x4(&worldMatrix, W);
        isDirty = false;
    }

    XMFLOAT3 GetWorldPosition() const {
        return XMFLOAT3(worldMatrix._41, worldMatrix._42, worldMatrix._43);
    }

    XMFLOAT3 GetWorldForward() const {
        XMVECTOR fwd = XMVectorSet(worldMatrix._31, worldMatrix._32, worldMatrix._33, 0.0f);
        fwd = XMVector3Normalize(fwd);
        XMFLOAT3 result;
        XMStoreFloat3(&result, fwd);
        return result;
    }

    XMFLOAT3 GetWorldUp() const {
        XMVECTOR up = XMVectorSet(worldMatrix._21, worldMatrix._22, worldMatrix._23, 0.0f);
        up = XMVector3Normalize(up);
        XMFLOAT3 result;
        XMStoreFloat3(&result, up);
        return result;
    }

    XMFLOAT3 GetWorldRight() const {
        XMVECTOR right = XMVectorSet(worldMatrix._11, worldMatrix._12, worldMatrix._13, 0.0f);
        right = XMVector3Normalize(right);
        XMFLOAT3 result;
        XMStoreFloat3(&result, right);
        return result;
    }
};

} // namespace EngineScene
