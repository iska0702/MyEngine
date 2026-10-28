#pragma once
#if defined(_WIN32)
#include <DirectXMath.h>
#else
#include "Platform/DirectXMath/DirectXMath.h"
#endif
#include <vector>
#include <cstdint>
#include <ostream>
#include <istream>
#include <string>

namespace EnginePhysics {
struct ColliderSettings {
    DirectX::XMFLOAT3 offset = {0,0,0};
    DirectX::XMFLOAT3 size = {1,1,1}; // Multiplier relative to mesh bounds.
};
struct CollisionMeshData {
    std::vector<DirectX::XMFLOAT3> vertices;
    std::vector<uint32_t> indices;
};
inline void WriteCollider(std::ostream& out, const ColliderSettings& c) {
    out << "COLLIDER_OFFSET " << c.offset.x << ' ' << c.offset.y << ' ' << c.offset.z << '\n';
    out << "COLLIDER_SIZE " << c.size.x << ' ' << c.size.y << ' ' << c.size.z << '\n';
}
inline bool ReadCollider(const std::string& tag, std::istream& in, ColliderSettings& c) {
    if (tag == "COLLIDER_OFFSET") { in >> c.offset.x >> c.offset.y >> c.offset.z; return true; }
    if (tag == "COLLIDER_SIZE") { in >> c.size.x >> c.size.y >> c.size.z; return true; }
    return false;
}
}
