#pragma once
#include <DirectXMath.h>
#include <cmath>
#include <algorithm>

namespace EngineRender {
// D3D clip space: -w <= x,y <= w and 0 <= z <= w.
struct ShadowFrustum {
    DirectX::XMFLOAT4 planes[6];
    explicit ShadowFrustum(DirectX::FXMMATRIX worldToClip) {
        DirectX::XMFLOAT4X4 m;
        DirectX::XMStoreFloat4x4(&m, worldToClip);
        const DirectX::XMFLOAT4 x(m._11,m._21,m._31,m._41);
        const DirectX::XMFLOAT4 y(m._12,m._22,m._32,m._42);
        const DirectX::XMFLOAT4 z(m._13,m._23,m._33,m._43);
        const DirectX::XMFLOAT4 w(m._14,m._24,m._34,m._44);
        auto sum=[](const auto& a,const auto& b,float sign) {
            return DirectX::XMFLOAT4(a.x+sign*b.x,a.y+sign*b.y,a.z+sign*b.z,a.w+sign*b.w);
        };
        planes[0]=sum(w,x,1); planes[1]=sum(w,x,-1);
        planes[2]=sum(w,y,1); planes[3]=sum(w,y,-1);
        planes[4]=z; planes[5]=sum(w,z,-1);
    }
    bool Intersects(const DirectX::XMFLOAT3& center,float radius) const {
        if (!(radius > 0) || !std::isfinite(radius) || !std::isfinite(center.x) ||
            !std::isfinite(center.y) || !std::isfinite(center.z)) return true;
        radius += (std::max)(0.001f, radius * 0.0001f);
        for (const auto& p:planes) {
            const float len=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
            const float distance=p.x*center.x+p.y*center.y+p.z*center.z+p.w;
            if (!std::isfinite(len) || !std::isfinite(distance)) return true;
            if (distance < -radius*len) return false;
        }
        return true;
    }
};
}
