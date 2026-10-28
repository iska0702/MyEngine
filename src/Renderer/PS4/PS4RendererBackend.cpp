#include "PS4RendererBackend.h"
#include "Renderer/Camera.h"
#include <imgui.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>

using namespace DirectX;

namespace EngineRender {

PS4RendererBackend::PS4RendererBackend() {
    m_capabilities.deviceName = "PlayStation 4 GPU (Liverpool AMD GCN)";
    m_capabilities.apiVersion = "OpenOrbis Native";
    m_capabilities.SupportsRayTracing = false;
    m_capabilities.SupportsMeshShaders = false;
    m_capabilities.SupportsMetalFX = false;
    m_capabilities.SupportsHDR = true;
    m_capabilities.SupportsBCTextures = true;
    m_capabilities.SupportsMSAA = true;
}

PS4RendererBackend::~PS4RendererBackend() {
    Destroy();
}

bool PS4RendererBackend::Initialize(void* nativeWindowHandle, int width, int height, Camera* camera) {
    m_width = width > 0 ? width : 1920;
    m_height = height > 0 ? height : 1080;
    m_camera = camera;
    m_ps4Window = static_cast<EnginePlatform::PS4Window*>(nativeWindowHandle);

    if (!m_device.Initialize(m_width, m_height)) {
        return false;
    }

#ifndef ENGINE_RELEASE
    if (!m_imguiInitialized) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        // Build Font Atlas
        unsigned char* texPixels = nullptr;
        int texW = 0, texH = 0;
        io.Fonts->GetTexDataAsRGBA32(&texPixels, &texW, &texH);
        if (texPixels && texW > 0 && texH > 0) {
            m_fontAtlasPixels.assign(texPixels, texPixels + (size_t)texW * texH * 4);
            m_fontAtlasWidth = texW;
            m_fontAtlasHeight = texH;
            io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
        }
        m_imguiInitialized = true;
        std::cout << "[PS4RendererBackend] ImGui initialized (" << m_fontAtlasWidth << "x" << m_fontAtlasHeight << " font atlas)" << std::endl;
    }
#endif

    std::cout << "[PS4RendererBackend] Initialized backend (" << m_width << "x" << m_height
              << ", window=" << m_ps4Window << ")" << std::endl;
    return true;
}

void PS4RendererBackend::Destroy() {
    m_dynamicModels.clear();
    m_staticVertices.clear();
    m_staticIndices.clear();
    m_device.Shutdown();
}

void PS4RendererBackend::Resize(int width, int height) {
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
        m_device.Shutdown();
        m_device.Initialize(width, height);
#ifndef ENGINE_RELEASE
        if (m_imguiInitialized) {
            ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
        }
#endif
    }
}

void PS4RendererBackend::BeginUI() {
#ifndef ENGINE_RELEASE
    if (m_imguiInitialized) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(m_width), static_cast<float>(m_height));
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
    }
#endif
}

const GraphicsCapabilities& PS4RendererBackend::GetCapabilities() const {
    return m_capabilities;
}

void PS4RendererBackend::UploadStaticGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    m_staticVertices = vertices;
    m_staticIndices = indices;
    std::cout << "[PS4RendererBackend] Uploaded " << vertices.size() << " static vertices, "
              << indices.size() << " static indices" << std::endl;
}

uint32_t PS4RendererBackend::CreateDynamicModel(const std::string& name,
                                               const std::vector<Vertex>& vertices,
                                               const std::vector<uint32_t>& indices,
                                               const PBRMaterial& material) {
    static uint32_t s_nextId = 1000;
    uint32_t id = s_nextId++;

    PS4DynamicModelEntry entry;
    entry.id = id;
    entry.name = name;
    entry.vertices = vertices;
    entry.indices = indices;
    entry.material = material;
    XMStoreFloat4x4(&entry.worldMatrix, XMMatrixIdentity());
    m_dynamicModels.push_back(std::move(entry));

    return id;
}

bool PS4RendererBackend::UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Vertex>& vertices) {
    for (auto& dm : m_dynamicModels) {
        if (dm.id == modelId) {
            dm.vertices = vertices;
            return true;
        }
    }
    return false;
}

bool PS4RendererBackend::UpdateDynamicModelTopology(uint32_t modelId,
                                                   const std::vector<Vertex>& vertices,
                                                   const std::vector<uint32_t>& indices) {
    for (auto& dm : m_dynamicModels) {
        if (dm.id == modelId) {
            dm.vertices = vertices;
            dm.indices = indices;
            return true;
        }
    }
    return false;
}

bool PS4RendererBackend::RemoveDynamicModel(uint32_t modelId) {
    for (auto it = m_dynamicModels.begin(); it != m_dynamicModels.end(); ++it) {
        if (it->id == modelId) {
            m_dynamicModels.erase(it);
            return true;
        }
    }
    return false;
}

void PS4RendererBackend::DrawMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                  uint32_t startIndex, uint32_t indexCount, int32_t baseVertex,
                                  const XMMATRIX& worldMatrix, const PBRMaterial& material,
                                  uint32_t* frameBuffer, float* depthBuffer, int width, int height,
                                  const XMMATRIX& viewProj, const XMFLOAT3& lightDir, const XMFLOAT3& camPos) {
    if (!frameBuffer || !depthBuffer || vertices.empty() || indices.empty() || indexCount == 0) return;
    if (startIndex + indexCount > indices.size()) return;

    XMMATRIX wvp = worldMatrix * viewProj;

    struct ClipVertex {
        XMVECTOR clip;
        XMFLOAT3 normal;
        XMFLOAT4 color;
    };

    struct ScreenVertex {
        float sx, sy, sz;
        float invW;
        XMFLOAT3 normal;
        XMFLOAT4 color;
    };

    auto ProjectToScreen = [&](const ClipVertex& cv) -> ScreenVertex {
        float w = XMVectorGetW(cv.clip);
        float invW = 1.0f / (std::max)(w, 0.001f);
        float ndcX = XMVectorGetX(cv.clip) * invW;
        float ndcY = XMVectorGetY(cv.clip) * invW;
        float ndcZ = XMVectorGetZ(cv.clip) * invW;

        ScreenVertex sv;
        sv.sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
        sv.sy = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height);
        sv.sz = ndcZ;
        sv.invW = invW;
        sv.normal = cv.normal;
        sv.color = cv.color;
        return sv;
    };

    size_t drawnPixels = 0;
    size_t clippedTris = 0;
    size_t culledTris = 0;
    size_t passedTris = 0;

    auto RasterizeTriangle = [&](const ScreenVertex& v0, const ScreenVertex& v1, const ScreenVertex& v2) {
        // Back-face culling (2D cross product in screen space, Y increases downwards)
        float area = (v1.sx - v0.sx) * (v2.sy - v0.sy) - (v1.sy - v0.sy) * (v2.sx - v0.sx);
        if (area <= 0.0f) {
            culledTris++;
            return;
        }
        passedTris++;

        float invArea = 1.0f / area;

        // Triangle face normal and lighting computed once per triangle
        float nx = (v0.normal.x + v1.normal.x + v2.normal.x) * (1.0f / 3.0f);
        float ny = (v0.normal.y + v1.normal.y + v2.normal.y) * (1.0f / 3.0f);
        float nz = (v0.normal.z + v1.normal.z + v2.normal.z) * (1.0f / 3.0f);
        float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }

        float nDotL = (std::max)(0.0f, -(nx * lightDir.x + ny * lightDir.y + nz * lightDir.z));
        float lightFactorR = (m_ambientColor.x + m_lightColor.x * nDotL) * material.albedo.x;
        float lightFactorG = (m_ambientColor.y + m_lightColor.y * nDotL) * material.albedo.y;
        float lightFactorB = (m_ambientColor.z + m_lightColor.z * nDotL) * material.albedo.z;

        // Check if triangle has uniform vertex colors
        bool uniformColor = (std::abs(v0.color.x - v1.color.x) < 0.01f && std::abs(v0.color.x - v2.color.x) < 0.01f &&
                             std::abs(v0.color.y - v1.color.y) < 0.01f && std::abs(v0.color.y - v2.color.y) < 0.01f &&
                             std::abs(v0.color.z - v1.color.z) < 0.01f && std::abs(v0.color.z - v2.color.z) < 0.01f);

        uint32_t packedColor = 0;
        if (uniformColor) {
            uint8_t outR = static_cast<uint8_t>((std::min)(255.0f, v0.color.x * lightFactorR * 255.0f));
            uint8_t outG = static_cast<uint8_t>((std::min)(255.0f, v0.color.y * lightFactorG * 255.0f));
            uint8_t outB = static_cast<uint8_t>((std::min)(255.0f, v0.color.z * lightFactorB * 255.0f));
            packedColor = 0xFF000000 | (static_cast<uint32_t>(outR) << 16) | (static_cast<uint32_t>(outG) << 8) | static_cast<uint32_t>(outB);
        }

        int minX = (std::max)(0, static_cast<int>(std::floor((std::min)({ v0.sx, v1.sx, v2.sx }))));
        int maxX = (std::min)(width - 1, static_cast<int>(std::ceil((std::max)({ v0.sx, v1.sx, v2.sx }))));
        int minY = (std::max)(0, static_cast<int>(std::floor((std::min)({ v0.sy, v1.sy, v2.sy }))));
        int maxY = (std::min)(height - 1, static_cast<int>(std::ceil((std::max)({ v0.sy, v1.sy, v2.sy }))));

        // Edge stepping constants
        float dx0 = v2.sx - v1.sx;
        float dy0 = v2.sy - v1.sy;
        float dx1 = v0.sx - v2.sx;
        float dy1 = v0.sy - v2.sy;
        float dx2 = v1.sx - v0.sx;
        float dy2 = v1.sy - v0.sy;

        for (int y = minY; y <= maxY; ++y) {
            float py = static_cast<float>(y) + 0.5f;
            int rowOffset = y * width;

            // Initial edge values at minX + 0.5f
            float startPx = static_cast<float>(minX) + 0.5f;
            float row_w0 = dx0 * (py - v1.sy) - dy0 * (startPx - v1.sx);
            float row_w1 = dx1 * (py - v2.sy) - dy1 * (startPx - v2.sx);
            float row_w2 = dx2 * (py - v0.sy) - dy2 * (startPx - v0.sx);

            for (int x = minX; x <= maxX; ++x) {
                if (row_w0 >= 0.0f && row_w1 >= 0.0f && row_w2 >= 0.0f) {
                    float b0 = row_w0 * invArea;
                    float b1 = row_w1 * invArea;
                    float b2 = row_w2 * invArea;

                    float depth = b0 * v0.sz + b1 * v1.sz + b2 * v2.sz;
                    int pixelIdx = rowOffset + x;

                    if (depth >= 0.0f && depth < depthBuffer[pixelIdx]) {
                        depthBuffer[pixelIdx] = depth;
                        drawnPixels++;

                        float interpInvW = b0 * v0.invW + b1 * v1.invW + b2 * v2.invW;
                        float eyeDist = (interpInvW > 1e-5f) ? (1.0f / interpInvW) : 1000.0f;

                        float fog = 0.0f;
                        if (m_fogEnabled && eyeDist > m_fogNear) {
                            fog = (eyeDist - m_fogNear) / (m_fogFar - m_fogNear);
                            if (fog > 1.0f) fog = 1.0f;
                        }

                        if (uniformColor && fog <= 0.001f) {
                            frameBuffer[pixelIdx] = packedColor;
                        } else {
                            float cr = uniformColor ? (v0.color.x * lightFactorR) : ((b0 * v0.color.x + b1 * v1.color.x + b2 * v2.color.x) * lightFactorR);
                            float cg = uniformColor ? (v0.color.y * lightFactorG) : ((b0 * v0.color.y + b1 * v1.color.y + b2 * v2.color.y) * lightFactorG);
                            float cb = uniformColor ? (v0.color.z * lightFactorB) : ((b0 * v0.color.z + b1 * v1.color.z + b2 * v2.color.z) * lightFactorB);

                            if (fog > 0.001f) {
                                float invFog = 1.0f - fog;
                                cr = cr * invFog + m_fogColor.x * fog;
                                cg = cg * invFog + m_fogColor.y * fog;
                                cb = cb * invFog + m_fogColor.z * fog;
                            }

                            uint8_t outR = static_cast<uint8_t>((std::min)(255.0f, cr * 255.0f));
                            uint8_t outG = static_cast<uint8_t>((std::min)(255.0f, cg * 255.0f));
                            uint8_t outB = static_cast<uint8_t>((std::min)(255.0f, cb * 255.0f));

                            frameBuffer[pixelIdx] = 0xFF000000 | (static_cast<uint32_t>(outR) << 16) | (static_cast<uint32_t>(outG) << 8) | static_cast<uint32_t>(outB);
                        }
                    }
                }
                // Step to next pixel horizontally
                row_w0 -= dy0;
                row_w1 -= dy1;
                row_w2 -= dy2;
            }
        }
    };

    const float nearW = 0.1f;
    size_t triCount = indexCount / 3;

    for (size_t t = 0; t < triCount; ++t) {
        ClipVertex cv[3];
        bool validIndices = true;
        for (int k = 0; k < 3; ++k) {
            int32_t vidx = static_cast<int32_t>(indices[startIndex + t * 3 + k]) + baseVertex;
            if (vidx < 0 || static_cast<size_t>(vidx) >= vertices.size()) {
                validIndices = false;
                break;
            }
            XMVECTOR pos = XMVectorSet(vertices[vidx].position.x, vertices[vidx].position.y, vertices[vidx].position.z, 1.0f);
            cv[k].clip = XMVector4Transform(pos, wvp);

            XMVECTOR norm = XMVectorSet(vertices[vidx].normal.x, vertices[vidx].normal.y, vertices[vidx].normal.z, 0.0f);
            XMVECTOR worldNorm = XMVector3Normalize(XMVector4Transform(norm, worldMatrix));
            XMStoreFloat3(&cv[k].normal, worldNorm);
            cv[k].color = vertices[vidx].color;
        }
        if (!validIndices) continue;

        int inCount = 0;
        for (int k = 0; k < 3; ++k) {
            if (XMVectorGetW(cv[k].clip) >= nearW) inCount++;
        }

        if (inCount == 3) {
            RasterizeTriangle(ProjectToScreen(cv[0]), ProjectToScreen(cv[1]), ProjectToScreen(cv[2]));
        } else if (inCount == 0) {
            clippedTris++;
        } else {
            // Triangle intersects near plane: Sutherland-Hodgman clip against W >= nearW
            ClipVertex polyOut[4];
            int outCount = 0;
            for (int k = 0; k < 3; ++k) {
                const auto& A = cv[k];
                const auto& B = cv[(k + 1) % 3];
                float wA = XMVectorGetW(A.clip);
                float wB = XMVectorGetW(B.clip);
                bool inA = (wA >= nearW);
                bool inB = (wB >= nearW);

                if (inB) {
                    if (!inA) {
                        float t = (nearW - wA) / (wB - wA);
                        ClipVertex I;
                        I.clip = XMVectorLerp(A.clip, B.clip, t);
                        I.normal = { A.normal.x + t * (B.normal.x - A.normal.x),
                                     A.normal.y + t * (B.normal.y - A.normal.y),
                                     A.normal.z + t * (B.normal.z - A.normal.z) };
                        I.color = { A.color.x + t * (B.color.x - A.color.x),
                                    A.color.y + t * (B.color.y - A.color.y),
                                    A.color.z + t * (B.color.z - A.color.z),
                                    A.color.w + t * (B.color.w - A.color.w) };
                        if (outCount < 4) polyOut[outCount++] = I;
                    }
                    if (outCount < 4) polyOut[outCount++] = B;
                } else if (inA) {
                    float t = (nearW - wA) / (wB - wA);
                    ClipVertex I;
                    I.clip = XMVectorLerp(A.clip, B.clip, t);
                    I.normal = { A.normal.x + t * (B.normal.x - A.normal.x),
                                 A.normal.y + t * (B.normal.y - A.normal.y),
                                 A.normal.z + t * (B.normal.z - A.normal.z) };
                    I.color = { A.color.x + t * (B.color.x - A.color.x),
                                A.color.y + t * (B.color.y - A.color.y),
                                A.color.z + t * (B.color.z - A.color.z),
                                A.color.w + t * (B.color.w - A.color.w) };
                    if (outCount < 4) polyOut[outCount++] = I;
                }
            }

            if (outCount >= 3) {
                ScreenVertex sv0 = ProjectToScreen(polyOut[0]);
                ScreenVertex sv1 = ProjectToScreen(polyOut[1]);
                ScreenVertex sv2 = ProjectToScreen(polyOut[2]);
                RasterizeTriangle(sv0, sv1, sv2);
                if (outCount == 4) {
                    ScreenVertex sv3 = ProjectToScreen(polyOut[3]);
                    RasterizeTriangle(sv0, sv2, sv3);
                }
            } else {
                clippedTris++;
            }
        }
    }
}

// Fast pseudo-random integer hash for procedural atmospheric noise
static inline float Hash2D(int x, int y) {
    int n = x * 374761393 + y * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    return static_cast<float>(n & 0x7FFFFFFF) * (1.0f / 2147483647.0f);
}

// Smooth continuous 2D value noise
static inline float Noise2D(float x, float y) {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);

    // Quintic smooth interpolation curve
    float sx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float sy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    float h00 = Hash2D(ix, iy);
    float h10 = Hash2D(ix + 1, iy);
    float h01 = Hash2D(ix, iy + 1);
    float h11 = Hash2D(ix + 1, iy + 1);

    float nx0 = h00 + (h10 - h00) * sx;
    float nx1 = h01 + (h11 - h01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

// Multi-octave Fractal Brownian Motion for volumetric clouds
static inline float FBM2D(float x, float y) {
    float val = 0.0f;
    val += 0.5000f * Noise2D(x, y);
    val += 0.2500f * Noise2D(x * 2.05f, y * 2.05f);
    val += 0.1250f * Noise2D(x * 4.10f, y * 4.10f);
    return val * 1.14f;
}

void PS4RendererBackend::RenderAtmosphericSkyAndClouds(uint32_t* frameBuffer, int width, int height,
                                                      const DirectX::XMMATRIX& invViewProj,
                                                      const DirectX::XMFLOAT3& camPos) {
    // Sun position opposite to light direction
    float sunX = -m_lightDir.x;
    float sunY = -m_lightDir.y;
    float sunZ = -m_lightDir.z;
    float sLen = std::sqrt(sunX * sunX + sunY * sunY + sunZ * sunZ);
    if (sLen > 1e-6f) { sunX /= sLen; sunY /= sLen; sunZ /= sLen; }

    float sunElev = sunY;

    // Day / sunset / night atmospheric palette blending
    float dayFactor = (std::clamp)(sunElev * 3.0f + 0.1f, 0.0f, 1.0f);
    float sunsetFactor = (std::clamp)(1.0f - std::abs(sunElev - 0.1f) * 4.0f, 0.0f, 1.0f) * (1.0f - dayFactor * 0.5f);

    DirectX::XMFLOAT3 dayHorizon = { 0.70f, 0.80f, 0.94f };
    DirectX::XMFLOAT3 sunsetHorizon = { 0.95f, 0.55f, 0.35f };
    DirectX::XMFLOAT3 nightHorizon = { 0.04f, 0.05f, 0.12f };

    DirectX::XMFLOAT3 dayZenith = { m_skyColor.x * 0.9f, m_skyColor.y * 1.05f, m_skyColor.z * 1.2f };
    DirectX::XMFLOAT3 sunsetZenith = { 0.25f, 0.30f, 0.55f };
    DirectX::XMFLOAT3 nightZenith = { 0.01f, 0.015f, 0.04f };

    DirectX::XMFLOAT3 horizonColor = {
        (dayHorizon.x * dayFactor + nightHorizon.x * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetHorizon.x * sunsetFactor,
        (dayHorizon.y * dayFactor + nightHorizon.y * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetHorizon.y * sunsetFactor,
        (dayHorizon.z * dayFactor + nightHorizon.z * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetHorizon.z * sunsetFactor
    };

    DirectX::XMFLOAT3 zenithColor = {
        (dayZenith.x * dayFactor + nightZenith.x * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetZenith.x * sunsetFactor,
        (dayZenith.y * dayFactor + nightZenith.y * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetZenith.y * sunsetFactor,
        (dayZenith.z * dayFactor + nightZenith.z * (1.0f - dayFactor)) * (1.0f - sunsetFactor) + sunsetZenith.z * sunsetFactor
    };

    // Storm overcast darkening
    float overcast = (std::clamp)((m_cloudDensity - 0.85f) / 1.6f, 0.0f, 1.0f);
    DirectX::XMFLOAT3 stormHorizon = { 0.30f, 0.34f, 0.42f };
    DirectX::XMFLOAT3 stormZenith = { 0.18f, 0.20f, 0.28f };
    horizonColor.x = horizonColor.x * (1.0f - overcast * 0.7f) + stormHorizon.x * (overcast * 0.7f);
    horizonColor.y = horizonColor.y * (1.0f - overcast * 0.7f) + stormHorizon.y * (overcast * 0.7f);
    horizonColor.z = horizonColor.z * (1.0f - overcast * 0.7f) + stormHorizon.z * (overcast * 0.7f);
    zenithColor.x = zenithColor.x * (1.0f - overcast * 0.7f) + stormZenith.x * (overcast * 0.7f);
    zenithColor.y = zenithColor.y * (1.0f - overcast * 0.7f) + stormZenith.y * (overcast * 0.7f);
    zenithColor.z = zenithColor.z * (1.0f - overcast * 0.7f) + stormZenith.z * (overcast * 0.7f);

    // Update fog color to match atmospheric horizon
    m_fogColor = horizonColor;

    // Fast 4x4 block stepping for 60 FPS software rasterization
    const int BLOCK = 4;
    int blocksX = (width + BLOCK - 1) / BLOCK;
    int blocksY = (height + BLOCK - 1) / BLOCK;

    float invW = 1.0f / static_cast<float>(width);
    float invH = 1.0f / static_cast<float>(height);

    for (int by = 0; by < blocksY; ++by) {
        int py = by * BLOCK + BLOCK / 2;
        if (py >= height) py = height - 1;

        float ndcY = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) * invH;

        XMVECTOR clipL = XMVectorSet(-1.0f, ndcY, 1.0f, 1.0f);
        XMVECTOR clipR = XMVectorSet( 1.0f, ndcY, 1.0f, 1.0f);

        XMVECTOR wL = XMVector4Transform(clipL, invViewProj);
        XMVECTOR wR = XMVector4Transform(clipR, invViewProj);

        float invW_L = 1.0f / (std::max)(1e-5f, XMVectorGetW(wL));
        float invW_R = 1.0f / (std::max)(1e-5f, XMVectorGetW(wR));

        XMFLOAT3 dirL = {
            XMVectorGetX(wL) * invW_L - camPos.x,
            XMVectorGetY(wL) * invW_L - camPos.y,
            XMVectorGetZ(wL) * invW_L - camPos.z
        };
        XMFLOAT3 dirR = {
            XMVectorGetX(wR) * invW_R - camPos.x,
            XMVectorGetY(wR) * invW_R - camPos.y,
            XMVectorGetZ(wR) * invW_R - camPos.z
        };

        for (int bx = 0; bx < blocksX; ++bx) {
            int px = bx * BLOCK + BLOCK / 2;
            if (px >= width) px = width - 1;

            float tX = (static_cast<float>(px) + 0.5f) * invW;
            float rx = dirL.x + tX * (dirR.x - dirL.x);
            float ry = dirL.y + tX * (dirR.y - dirL.y);
            float rz = dirL.z + tX * (dirR.z - dirL.z);

            float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
            if (rLen > 1e-6f) {
                rx /= rLen; ry /= rLen; rz /= rLen;
            }

            // Atmospheric sky gradient
            float tSky = (std::clamp)(ry, 0.0f, 1.0f);
            float tPow = std::sqrt(tSky);

            float skyR = horizonColor.x + (zenithColor.x - horizonColor.x) * tPow;
            float skyG = horizonColor.y + (zenithColor.y - horizonColor.y) * tPow;
            float skyB = horizonColor.z + (zenithColor.z - horizonColor.z) * tPow;

            if (ry < 0.0f) {
                float groundFade = (std::clamp)(1.0f + ry * 4.0f, 0.0f, 1.0f);
                skyR *= groundFade;
                skyG *= groundFade;
                skyB *= groundFade;
            }

            // Sun disc & solar corona
            float sunDot = rx * sunX + ry * sunY + rz * sunZ;
            if (sunDot > 0.85f && dayFactor > 0.02f) {
                float glow = std::exp((sunDot - 1.0f) * 16.0f) * (1.0f - overcast * 0.7f);
                skyR += m_lightColor.x * glow * 1.2f;
                skyG += m_lightColor.y * glow * 1.0f;
                skyB += m_lightColor.z * glow * 0.6f;

                if (sunDot > 0.9982f) {
                    float disc = (sunDot - 0.9982f) / 0.0018f;
                    skyR += 3.0f * disc;
                    skyG += 2.8f * disc;
                    skyB += 2.2f * disc;
                }
            }

            // Procedural Volumetric Clouds layer
            if (m_cloudsEnabled && m_cloudDensity > 0.05f && ry > 0.02f) {
                const float cloudAltitude = 220.0f;
                float rayDist = cloudAltitude / ry;
                if (rayDist > 3500.0f) rayDist = 3500.0f;

                float driftSpeed = 0.012f + overcast * 0.025f;
                float wx = (camPos.x + rx * rayDist) * 0.0014f + m_cloudTime * driftSpeed;
                float wz = (camPos.z + rz * rayDist) * 0.0014f + m_cloudTime * (driftSpeed * 0.4f);

                float cloudNoise = FBM2D(wx, wz);

                float coverageThreshold = 0.52f - (m_cloudDensity - 0.8f) * 0.20f;
                coverageThreshold = (std::clamp)(coverageThreshold, 0.15f, 0.80f);

                if (cloudNoise > coverageThreshold) {
                    float density = (cloudNoise - coverageThreshold) / (1.0f - coverageThreshold);
                    float horizonFade = (std::min)(1.0f, ry * 12.0f);
                    float cloudAlpha = (std::min)(1.0f, density * 2.2f) * horizonFade * (std::min)(1.0f, m_cloudDensity);

                    float sunLightFactor = 0.55f + 0.45f * (std::max)(0.0f, sunDot);
                    float cR = (0.80f + 0.20f * m_lightColor.x) * sunLightFactor;
                    float cG = (0.84f + 0.16f * m_lightColor.y) * sunLightFactor;
                    float cB = (0.90f + 0.10f * m_lightColor.z) * sunLightFactor;

                    if (overcast > 0.2f) {
                        cR *= (1.0f - overcast * 0.45f);
                        cG *= (1.0f - overcast * 0.42f);
                        cB *= (1.0f - overcast * 0.38f);
                    }

                    skyR = skyR * (1.0f - cloudAlpha) + cR * cloudAlpha;
                    skyG = skyG * (1.0f - cloudAlpha) + cG * cloudAlpha;
                    skyB = skyB * (1.0f - cloudAlpha) + cB * cloudAlpha;
                }
            }

            uint8_t outR = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, skyR * 255.0f)));
            uint8_t outG = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, skyG * 255.0f)));
            uint8_t outB = static_cast<uint8_t>((std::min)(255.0f, (std::max)(0.0f, skyB * 255.0f)));
            uint32_t packedPixel = 0xFF000000 | (static_cast<uint32_t>(outR) << 16) | (static_cast<uint32_t>(outG) << 8) | static_cast<uint32_t>(outB);

            int yStart = by * BLOCK;
            int yEnd = (std::min)(yStart + BLOCK, height);
            int xStart = bx * BLOCK;
            int xEnd = (std::min)(xStart + BLOCK, width);

            for (int y = yStart; y < yEnd; ++y) {
                uint32_t* row = frameBuffer + y * width;
                for (int x = xStart; x < xEnd; ++x) {
                    row[x] = packedPixel;
                }
            }
        }
    }
}

void PS4RendererBackend::RenderRainStreaks(uint32_t* frameBuffer, int width, int height, float dt) {
    if (m_rainIntensity <= 0.01f || !frameBuffer) return;

    int streakCount = static_cast<int>(m_rainIntensity * 1400.0f);
    if (streakCount > 3000) streakCount = 3000;

    const float windAngle = -0.18f;

    for (int i = 0; i < streakCount; ++i) {
        uint32_t seed = static_cast<uint32_t>(i * 1973 + 9277);
        float randX = static_cast<float>((seed ^ (seed >> 7)) % width);
        float randPhase = static_cast<float>((seed * 37) % 1000) * 0.001f;
        float length = 14.0f + static_cast<float>((seed * 13) % 18);

        float progress = std::fmod(m_cloudTime * 2.2f + randPhase, 1.0f);
        float startY = progress * static_cast<float>(height + 60) - 30.0f;
        float startX = randX + (progress - 0.5f) * windAngle * static_cast<float>(height);

        int len = static_cast<int>(length);
        for (int l = 0; l < len; ++l) {
            int px = static_cast<int>(startX + static_cast<float>(l) * windAngle);
            int py = static_cast<int>(startY + static_cast<float>(l));

            if (px >= 0 && px < width && py >= 0 && py < height) {
                int idx = py * width + px;
                uint32_t bg = frameBuffer[idx];
                uint8_t bgR = (bg >> 16) & 0xFF;
                uint8_t bgG = (bg >> 8) & 0xFF;
                uint8_t bgB = bg & 0xFF;

                uint8_t r = (bgR + 195) >> 1;
                uint8_t g = (bgG + 215) >> 1;
                uint8_t b = (bgB + 250) >> 1;

                frameBuffer[idx] = 0xFF000000 | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
            }
        }
    }
}

void PS4RendererBackend::RenderPBRScene(uint32_t* frameBuffer, int width, int height, float dt) {
    if (!frameBuffer || !m_camera) return;

    static int s_sceneDiagFrames = 0;
    if (s_sceneDiagFrames < 3) {
        XMFLOAT3 cp = m_camera->GetPosition();
        XMFLOAT3 cf = m_camera->GetForwardVector();
        std::cout << "[RenderPBRScene] camPos=(" << cp.x << "," << cp.y << "," << cp.z 
                  << ") camFwd=(" << cf.x << "," << cf.y << "," << cf.z << ") items=" << m_sceneDrawItems.size() << std::endl;
        s_sceneDiagFrames++;
    }

    // 1. Clear depth buffer
    m_device.ClearDepthBuffer(1.0f);
    float* depthBuffer = m_device.GetDepthBuffer();

    // 2. Setup Camera View-Projection
    XMMATRIX view = m_camera->GetViewMatrix();
    float aspect = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : (16.0f / 9.0f);
    XMMATRIX proj = m_camera->GetProjectionMatrix(DirectX::XM_PIDIV4, aspect, 0.1f, 1000.0f);
    XMMATRIX viewProj = view * proj;
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
    XMFLOAT3 camPos = m_camera->GetPosition();

    // 3. Render Atmospheric Sky Dome, Volumetric Clouds & Sun Disc
    RenderAtmosphericSkyAndClouds(frameBuffer, width, height, invViewProj, camPos);

    // 4. Render Scene Models
    if (!m_sceneDrawItems.empty()) {
        for (const auto& item : m_sceneDrawItems) {
            if (!item.visible || item.indexCount == 0) continue;
            XMMATRIX world = XMLoadFloat4x4(&item.worldMatrix);
            DrawMesh(m_staticVertices, m_staticIndices,
                     item.startIndex, item.indexCount, item.baseVertex,
                     world, item.material,
                     frameBuffer, depthBuffer, width, height, viewProj, m_lightDir, camPos);
        }
    } else if (!m_staticVertices.empty() && !m_staticIndices.empty()) {
        PBRMaterial defaultMat;
        defaultMat.albedo = XMFLOAT3(0.8f, 0.8f, 0.8f);
        defaultMat.roughness = 0.5f;
        defaultMat.metallic = 0.0f;
        DrawMesh(m_staticVertices, m_staticIndices,
                  0, static_cast<uint32_t>(m_staticIndices.size()), 0,
                  XMMatrixIdentity(), defaultMat,
                  frameBuffer, depthBuffer, width, height, viewProj, m_lightDir, camPos);
    }

    // 5. Render Dynamic Models (Cloth, Character models)
    for (const auto& dm : m_dynamicModels) {
        if (!dm.visible || dm.vertices.empty() || dm.indices.empty()) continue;
        XMMATRIX world = XMLoadFloat4x4(&dm.worldMatrix);
        DrawMesh(dm.vertices, dm.indices,
                 0, static_cast<uint32_t>(dm.indices.size()), 0,
                 world, dm.material,
                 frameBuffer, depthBuffer, width, height, viewProj, m_lightDir, camPos);
    }

    // 6. Render Weather Rain Streaks
    if (m_rainIntensity > 0.01f) {
        RenderRainStreaks(frameBuffer, width, height, dt);
    }
}

void PS4RendererBackend::DrawImGui(ImDrawData* drawData, uint32_t* frameBuffer, int width, int height) {
    if (!drawData || !drawData->Valid || !frameBuffer || width <= 0 || height <= 0) return;

    ImVec2 clipOff = drawData->DisplayPos;
    int fontW = m_fontAtlasWidth;
    int fontH = m_fontAtlasHeight;
    const uint8_t* fontPixels = m_fontAtlasPixels.data();

    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        const ImDrawVert* vtxBuffer = cmdList->VtxBuffer.Data;
        const ImDrawIdx* idxBuffer = cmdList->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmdList->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd& pcmd = cmdList->CmdBuffer[cmd_i];
            if (pcmd.UserCallback != nullptr) {
                pcmd.UserCallback(cmdList, &pcmd);
                continue;
            }

            // Project scissor / clip rect into framebuffer coords
            int clipX0 = (std::max)(0, static_cast<int>(pcmd.ClipRect.x - clipOff.x));
            int clipY0 = (std::max)(0, static_cast<int>(pcmd.ClipRect.y - clipOff.y));
            int clipX1 = (std::min)(width, static_cast<int>(pcmd.ClipRect.z - clipOff.x));
            int clipY1 = (std::min)(height, static_cast<int>(pcmd.ClipRect.w - clipOff.y));

            if (clipX1 <= clipX0 || clipY1 <= clipY0) continue;

            bool isTextured = (pcmd.GetTexID() != 0 && fontPixels != nullptr && fontW > 0 && fontH > 0);

            for (unsigned int i = 0; i < pcmd.ElemCount; i += 3) {
                const ImDrawVert& v0 = vtxBuffer[idxBuffer[pcmd.IdxOffset + i + 0]];
                const ImDrawVert& v1 = vtxBuffer[idxBuffer[pcmd.IdxOffset + i + 1]];
                const ImDrawVert& v2 = vtxBuffer[idxBuffer[pcmd.IdxOffset + i + 2]];

                // Bounding box of triangle
                float minX_f = (std::min)({ v0.pos.x, v1.pos.x, v2.pos.x });
                float maxX_f = (std::max)({ v0.pos.x, v1.pos.x, v2.pos.x });
                float minY_f = (std::min)({ v0.pos.y, v1.pos.y, v2.pos.y });
                float maxY_f = (std::max)({ v0.pos.y, v1.pos.y, v2.pos.y });

                int triX0 = (std::max)(clipX0, static_cast<int>(minX_f));
                int triX1 = (std::min)(clipX1, static_cast<int>(maxX_f + 1.0f));
                int triY0 = (std::max)(clipY0, static_cast<int>(minY_f));
                int triY1 = (std::min)(clipY1, static_cast<int>(maxY_f + 1.0f));

                if (triX1 <= triX0 || triY1 <= triY0) continue;

                // Triangle 2D signed area (doubled)
                float det = (v1.pos.x - v0.pos.x) * (v2.pos.y - v0.pos.y) - (v1.pos.y - v0.pos.y) * (v2.pos.x - v0.pos.x);
                if (std::abs(det) < 1e-5f) continue;
                float invDet = 1.0f / det;

                // Extract vertex colors (ImU32: byte 0=R, 1=G, 2=B, 3=A)
                uint32_t c0 = v0.col, c1 = v1.col, c2 = v2.col;
                bool isFlatColor = (c0 == c1 && c1 == c2);
                float r0 = static_cast<float>(c0 & 0xFF);
                float g0 = static_cast<float>((c0 >> 8) & 0xFF);
                float b0 = static_cast<float>((c0 >> 16) & 0xFF);
                float a0 = static_cast<float>((c0 >> 24) & 0xFF) / 255.0f;

                float r1 = static_cast<float>(c1 & 0xFF);
                float g1 = static_cast<float>((c1 >> 8) & 0xFF);
                float b1 = static_cast<float>((c1 >> 16) & 0xFF);
                float a1 = static_cast<float>((c1 >> 24) & 0xFF) / 255.0f;

                float r2 = static_cast<float>(c2 & 0xFF);
                float g2 = static_cast<float>((c2 >> 8) & 0xFF);
                float b2 = static_cast<float>((c2 >> 16) & 0xFF);
                float a2 = static_cast<float>((c2 >> 24) & 0xFF) / 255.0f;

                for (int y = triY0; y < triY1; ++y) {
                    float py = y + 0.5f;
                    uint32_t* row = &frameBuffer[y * width];

                    for (int x = triX0; x < triX1; ++x) {
                        float px = x + 0.5f;

                        // Barycentric coordinates
                        float w0 = ((v1.pos.x - px) * (v2.pos.y - py) - (v1.pos.y - py) * (v2.pos.x - px)) * invDet;
                        float w1 = ((v2.pos.x - px) * (v0.pos.y - py) - (v2.pos.y - py) * (v0.pos.x - px)) * invDet;
                        float w2 = 1.0f - w0 - w1;

                        if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;

                        float r, g, b, alpha;
                        if (isFlatColor) {
                            r = r0; g = g0; b = b0; alpha = a0;
                        } else {
                            r = w0 * r0 + w1 * r1 + w2 * r2;
                            g = w0 * g0 + w1 * g1 + w2 * g2;
                            b = w0 * b0 + w1 * b1 + w2 * b2;
                            alpha = w0 * a0 + w1 * a1 + w2 * a2;
                        }

                        if (isTextured) {
                            float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
                            float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
                            int tx = std::clamp(static_cast<int>(u * fontW), 0, fontW - 1);
                            int ty = std::clamp(static_cast<int>(v * fontH), 0, fontH - 1);
                            const uint8_t* texPtr = &fontPixels[(ty * fontW + tx) * 4];
                            
                            // texPtr is RGBA (font glyph alpha mask in texPtr[3])
                            alpha *= (texPtr[3] / 255.0f);
                        }

                        if (alpha <= 0.003f) continue;

                        // Alpha blend into destination framebuffer (0xAARRGGBB)
                        uint32_t dst = row[x];
                        uint32_t dr = (dst >> 16) & 0xFF;
                        uint32_t dg = (dst >> 8) & 0xFF;
                        uint32_t db = dst & 0xFF;

                        uint32_t outR = static_cast<uint32_t>(r * alpha + dr * (1.0f - alpha));
                        uint32_t outG = static_cast<uint32_t>(g * alpha + dg * (1.0f - alpha));
                        uint32_t outB = static_cast<uint32_t>(b * alpha + db * (1.0f - alpha));

                        row[x] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
                    }
                }
            }
        }
    }

    // Draw virtual mouse pointer if cursor is enabled
    if (m_ps4Window && m_ps4Window->IsCursorEnabled()) {
        int cx = static_cast<int>(m_ps4Window->GetVirtualCursorX());
        int cy = static_cast<int>(m_ps4Window->GetVirtualCursorY());

        // Draw 10x10 crosshair cursor with dark outline
        for (int dy = -6; dy <= 6; ++dy) {
            for (int dx = -6; dx <= 6; ++dx) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < width && py >= 0 && py < height) {
                    bool isBorder = (std::abs(dx) == 5 && std::abs(dy) <= 5) || (std::abs(dy) == 5 && std::abs(dx) <= 5);
                    bool isCross = (dx == 0 && std::abs(dy) <= 4) || (dy == 0 && std::abs(dx) <= 4);
                    if (isCross) {
                        frameBuffer[py * width + px] = 0xFFFFF200; // Bright yellow crosshair
                    } else if (isBorder) {
                        frameBuffer[py * width + px] = 0xFF101010; // Dark outline
                    }
                }
            }
        }
    }
}

void PS4RendererBackend::Render(float dt) {
    if (!m_ps4Window) return;

    uint32_t* frameBuffer = m_ps4Window->GetCurrentFrameBuffer();
    if (!frameBuffer) return;

    // Render entire PBR scene
    RenderPBRScene(frameBuffer, m_width, m_height, dt);

#ifndef ENGINE_RELEASE
    if (m_imguiInitialized) {
        ImGui::Render();
        DrawImGui(ImGui::GetDrawData(), frameBuffer, m_width, m_height);
    }
#endif

    // Present to PS4 VideoOut
    m_ps4Window->SubmitFlip();

    if (m_vsyncEnabled) {
        m_ps4Window->WaitFlip();
    }

    m_ps4Window->SwapBuffers();
}

} // namespace EngineRender
