#pragma once
#include <DirectXMath.h>

namespace EngineRender {
inline void StoreDxrTransform(float (&destination)[3][4], DirectX::FXMMATRIX world) {
    // XMStoreFloat3x4 already converts DirectXMath's row-vector matrix to
    // DXR's three rows with translation in the fourth column.
    DirectX::XMFLOAT3X4 packed;
    DirectX::XMStoreFloat3x4(&packed, world);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            destination[row][column] = packed.m[row][column];
}
}
