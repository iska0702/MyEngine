#include "Renderer/DxrTransform.h"
#include "Renderer/Common/RenderTypes.h"
#include <cstddef>
#include <cmath>
#include <iostream>

static_assert(sizeof(EngineRender::PointLightData) == 32);
static_assert(sizeof(EngineRender::SpotLightData) == 64);
static_assert(offsetof(EngineRender::SceneConstantBuffer, pointLights) == 576,
              "Point lights must match HLSL constant-buffer register alignment");
static_assert(offsetof(EngineRender::SceneConstantBuffer, spotLights) == 1088);
static_assert(offsetof(EngineRender::RaytracingConstants, pointLights) == 160);
static_assert(offsetof(EngineRender::RaytracingConstants, spotLights) == 672);

int main() {
    using namespace DirectX;
    const XMMATRIX transforms[] = {
        XMMatrixIdentity(), XMMatrixTranslation(3, 5, 8),
        XMMatrixScaling(2, 0.5f, 3) * XMMatrixRotationRollPitchYaw(0.2f, 1.1f, -0.4f) * XMMatrixTranslation(-7, 2, 11)
    };
    const XMFLOAT3 points[] = { {0, 0, 0}, {1, 2, 3}, {-4, 0.7f, 5} };
    for (const auto& world : transforms) {
        float instance[3][4] = {};
        EngineRender::StoreDxrTransform(instance, world);
        for (const auto& point : points) {
            XMFLOAT3 expected;
            XMStoreFloat3(&expected, XMVector3TransformCoord(XMLoadFloat3(&point), world));
            const float values[] = { expected.x, expected.y, expected.z };
            for (int row = 0; row < 3; ++row) {
                const float actual = instance[row][0] * point.x + instance[row][1] * point.y + instance[row][2] * point.z + instance[row][3];
                if (std::fabs(actual - values[row]) > 0.0001f) {
                    std::cerr << "DXR and raster transforms disagree\n";
                    return 1;
                }
            }
        }
    }
    return 0;
}
