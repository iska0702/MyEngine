#include "../Renderer.h"

#if !defined(_WIN32)

#include "../Camera.h"
#include "MetalRendererBackend.h"
#include "../TextureStreamer.h"
#include "../Texture.h"
#ifndef ENGINE_RELEASE
#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"
#endif
#include "Storage/Crypto.h"
#include "Storage/PakCompression.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

std::vector<uint8_t> Renderer::LoadFromPak(const std::wstring& targetFile) {
    std::wstring normalizedTarget = targetFile;
    for (auto& c : normalizedTarget) if (c == L'\\') c = L'/';

    size_t cPos = normalizedTarget.find(L"Content/");
    if (cPos != std::wstring::npos) {
        normalizedTarget = normalizedTarget.substr(cPos);
    } else {
        size_t sPos = normalizedTarget.find(L"shaders/");
        if (sPos != std::wstring::npos) {
            normalizedTarget = normalizedTarget.substr(sPos);
        }
    }

    std::vector<uint8_t> masterKey = PakCrypto::GetMasterKey();

    std::vector<std::string> basePaths = {
        "",
        "Contents/",
        "../Resources/",
        "../Resources/Contents/",
        "MacBuild/Idontknow.app/Contents/Resources/",
        "MacBuild/Idontknow.app/Contents/Resources/Contents/"
    };

    for (const auto& base : basePaths) {
        for (int i = 1; i <= 10; ++i) {
            char pakPath[512];
            snprintf(pakPath, sizeof(pakPath), "%scontent%02d.pak", base.c_str(), i);
            if (base.empty()) {
                snprintf(pakPath, sizeof(pakPath), "Contents/content%02d.pak", i);
            }

            std::ifstream file(pakPath, std::ios::binary);
            if (!file.is_open()) continue;

            uint32_t magicOrNumFiles = 0;
            file.read((char*)&magicOrNumFiles, 4);

            if (magicOrNumFiles == 0x334B5045) { // 'EPAK'
                uint8_t indexIV[16];
                file.read((char*)indexIV, 16);

                uint32_t indexCipherSize = 0;
                file.read((char*)&indexCipherSize, 4);
                uint32_t indexPlainSize = 0;
                file.read((char*)&indexPlainSize, 4);

                if (indexCipherSize == 0 || indexCipherSize > 100 * 1024 * 1024) continue;

                std::vector<uint8_t> indexCipher(indexCipherSize);
                file.read((char*)indexCipher.data(), indexCipherSize);

                std::vector<uint8_t> plainIndex;
                if (!PakCrypto::DecryptBlock(masterKey.data(), masterKey.size(),
                                                 indexIV, 16,
                                                 indexCipher.data(), indexCipher.size(),
                                                 plainIndex) || plainIndex.size() < 4) {
                    continue;
                }

                uint32_t numFiles = *(uint32_t*)plainIndex.data();
                size_t entryOffset = 4;
                for (uint32_t j = 0; j < numFiles; ++j) {
                    if (entryOffset + 276 > plainIndex.size()) break;

                    wchar_t nameBuffer[129];
                    memcpy(nameBuffer, plainIndex.data() + entryOffset, 256);
                    nameBuffer[128] = L'\0';
                    entryOffset += 256;

                    uint32_t offset = *(uint32_t*)(plainIndex.data() + entryOffset);
                    entryOffset += 4;
                    uint32_t cipherTotalLength = *(uint32_t*)(plainIndex.data() + entryOffset);
                    entryOffset += 4;
                    uint32_t compressedLength = *(uint32_t*)(plainIndex.data() + entryOffset);
                    entryOffset += 4;
                    uint32_t uncompressedLength = *(uint32_t*)(plainIndex.data() + entryOffset);
                    entryOffset += 4;
                    uint32_t flags = *(uint32_t*)(plainIndex.data() + entryOffset);
                    entryOffset += 4;

                    std::wstring fileName(nameBuffer);
                    size_t last = fileName.find_last_not_of(L' ');
                    if (last != std::wstring::npos) fileName = fileName.substr(0, last + 1);
                    else fileName = L"";

                    std::wstring normalizedFile = fileName;
                    for (auto& c : normalizedFile) if (c == L'\\') c = L'/';

                    if (normalizedFile == normalizedTarget) {
                        if (cipherTotalLength <= 16) return {};

                        file.seekg(offset);
                        uint8_t fileIV[16];
                        file.read((char*)fileIV, 16);

                        uint32_t cipherLen = cipherTotalLength - 16;
                        std::vector<uint8_t> cipherData(cipherLen);
                        file.read((char*)cipherData.data(), cipherLen);

                        std::vector<uint8_t> decryptedData;
                        if (!PakCrypto::DecryptBlock(masterKey.data(), masterKey.size(),
                                                         fileIV, 16,
                                                         cipherData.data(), cipherData.size(),
                                                         decryptedData, PakCrypto::FileContext(std::filesystem::path(normalizedFile).u16string()))) {
                            return {};
                        }

                        if (flags & 1) {
                            std::vector<uint8_t> uncompressedData;
                            if (PakCompression::DecompressDeflate(decryptedData.data(), decryptedData.size(),
                                                                  uncompressedLength, uncompressedData)) {
                                return uncompressedData;
                            }
                            return {};
                        } else {
                            return decryptedData;
                        }
                    }
                }
            }
        }
    }
    return {};
}

std::shared_ptr<Texture> Renderer::LoadTexture(const std::string& filepath) {
    if (!m_textureStreamer) return nullptr;
    auto tex = m_textureStreamer->LoadTexture(filepath);
    if (tex) {
        m_textures.push_back(tex);
    }
    return tex;
}

void Renderer::UpdateSunFromAngles() {
    float azRad = m_sunAzimuth * (XM_PI / 180.0f);
    float elRad = m_sunElevation * (XM_PI / 180.0f);

    float y = sinf(elRad);
    float horiz = cosf(elRad);
    float x = horiz * sinf(azRad);
    float z = horiz * cosf(azRad);

    XMVECTOR dir = XMVector3Normalize(XMVectorSet(-x, -y, -z, 0.0f));
    XMStoreFloat3(&m_constantBufferData.lightDir, dir);
}

void Renderer::SetAnisotropy(int aniso) {
    m_anisotropy = aniso;
    if (m_metalBackend) {
        m_metalBackend->SetAnisotropy(aniso);
    }
}

void Renderer::SetReflexMode(ReflexMode mode) {
    m_reflexMode = mode;
}

void Renderer::SpawnDecal(XMVECTOR position, XMVECTOR normal, float scale, UINT textureIndex) {
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabsf(XMVectorGetY(normal)) > 0.99f) {
        up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, normal));
    up = XMVector3Normalize(XMVector3Cross(normal, right));

    XMMATRIX rot;
    rot.r[0] = right;
    rot.r[1] = up;
    rot.r[2] = normal;
    rot.r[3] = XMVectorSet(0, 0, 0, 1);

    XMMATRIX scaleM = XMMatrixScaling(scale, scale, scale);
    XMMATRIX transM = XMMatrixTranslationFromVector(position);
    XMMATRIX world = scaleM * rot * transM;

    Decal decal;
    decal.worldMatrix = world;
    decal.textureIndex = textureIndex;
    decal.color = XMFLOAT4(1, 1, 1, 1);
    decal.active = true;
    m_decals.push_back(decal);
}

void Renderer::ProcessLoadedTextures() {
    if (m_textureStreamer) {
        m_textureStreamer->Update();
    }
}

void Renderer::BuildMeshlets(const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<Renderer::Meshlet>& outMeshlets) {
    outMeshlets.clear();
    const uint32_t maxPrimitives = 126;

    size_t triCount = indices.size() / 3;
    for (size_t t = 0; t < triCount; t += maxPrimitives) {
        Meshlet m = {};
        m.PrimitiveOffset = (uint32_t)(t * 3);
        m.PrimitiveCount = (uint32_t)(std::min<size_t>)(maxPrimitives, triCount - t);
        m.VertexOffset = 0;
        m.VertexCount = (uint32_t)vertices.size();
        m.Center[0] = 0.0f; m.Center[1] = 0.0f; m.Center[2] = 0.0f;
        m.Radius = 100.0f;
        m.ConeNormal = XMFLOAT3(0, 1, 0);
        m.ConeCutoff = 0.0f;
        outMeshlets.push_back(m);
    }
}

static void AssignMeshletRange(UINT startIdx, UINT indexCount, const std::vector<Renderer::Meshlet>& meshlets, UINT& outStart, UINT& outCount) {
    outStart = 0;
    outCount = (UINT)meshlets.size();
}

void Renderer::UpdateModelMeshletRanges() {
    for (auto& model : m_models) {
        AssignMeshletRange(model.startIndexLocation, model.indexCount, m_cpuMeshlets, model.meshletStart, model.meshletCount);
    }
    for (auto& dm : m_dynamicModels) {
        AssignMeshletRange(dm.startIndexLocation, dm.indexCount, m_cpuMeshlets, dm.meshletStart, dm.meshletCount);
    }
}

Renderer::Renderer(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_renderWidth(width)
    , m_renderHeight(height)
    , m_camera(nullptr)
    , m_hwnd(nullptr)
    , m_skyColor(0.2f, 0.4f, 0.8f)
    , m_sunAzimuth(45.0f)
    , m_sunElevation(30.0f)
{
    UpdateSunFromAngles();
    m_constantBufferData.lightColor = XMFLOAT3(1.0f, 0.98f, 0.95f);
    m_baseLightColor = m_constantBufferData.lightColor;
}

Renderer::~Renderer() {
    Destroy();
}

bool Renderer::Initialize(HWND hwnd, Camera* camera) {
    m_hwnd = hwnd;
    m_camera = camera;

    std::vector<Renderer::Vertex>& verts = m_staticVertices;
    std::vector<uint32_t>& idx = m_staticIndices;
    verts.clear();
    idx.clear();

    auto V = [&](const Renderer::Vertex& v) { verts.push_back(v); };
    auto T = [&](uint32_t a, uint32_t b, uint32_t c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); };

    // Cube
    m_cubeBaseVertex = (INT)verts.size();
    m_cubeStartIndex = (UINT)idx.size();
    {
        struct Face { XMFLOAT3 n, r, u; XMFLOAT4 col; };
        Face faces[] = {
            {{ 0, 0,-1},{-1, 0, 0},{ 0, 1, 0}, {0.90f,0.90f,0.90f,1.0f}},
            {{ 0, 0, 1},{ 1, 0, 0},{ 0, 1, 0}, {0.65f,0.65f,0.65f,1.0f}},
            {{-1, 0, 0},{ 0, 0, 1},{ 0, 1, 0}, {0.75f,0.75f,0.75f,1.0f}},
            {{ 1, 0, 0},{ 0, 0,-1},{ 0, 1, 0}, {0.75f,0.75f,0.75f,1.0f}},
            {{ 0, 1, 0},{-1, 0, 0},{ 0, 0, 1}, {0.85f,0.85f,0.85f,1.0f}},
            {{ 0,-1, 0},{ 1, 0, 0},{ 0, 0, 1}, {0.50f,0.50f,0.50f,1.0f}},
        };
        for (int f = 0; f < 6; f++) {
            XMFLOAT3 n=faces[f].n, r=faces[f].r, u=faces[f].u;
            XMFLOAT4 col=faces[f].col;
            uint32_t b = (uint32_t)(verts.size() - m_cubeBaseVertex);
            auto c = [&](float rs, float us) {
                return Renderer::Vertex{{
                    n.x*.5f + r.x*rs*.5f + u.x*us*.5f,
                    n.y*.5f + r.y*rs*.5f + u.y*us*.5f,
                    n.z*.5f + r.z*rs*.5f + u.z*us*.5f },
                    n, col};
            };
            V(c(-1,-1)); V(c(1,-1)); V(c(1,1)); V(c(-1,1));
            T(b,b+1,b+2); T(b,b+2,b+3);
        }
    }
    m_cubeIndexCount = (UINT)idx.size() - m_cubeStartIndex;
    m_cubeVertexCount = (UINT)verts.size() - (UINT)m_cubeBaseVertex;

    // Plane
    m_planeBaseVertex = (INT)verts.size();
    m_planeStartIndex = (UINT)idx.size();
    V({{ -0.5f, 0, -0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{ -0.5f, 0,  0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{  0.5f, 0,  0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{  0.5f, 0, -0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    T(0,1,2); T(0,2,3);
    m_planeIndexCount = (UINT)idx.size() - m_planeStartIndex;
    m_planeVertexCount = (UINT)verts.size() - (UINT)m_planeBaseVertex;

    // Floor
    m_floorBaseVertex = (INT)verts.size();
    m_floorStartIndex = (UINT)idx.size();
    V({{ -50.0f, 0, -50.0f }, { 0,1,0 }, { 0.5f, 0.5f, 0.5f, 1.0f }});
    V({{ -50.0f, 0,  50.0f }, { 0,1,0 }, { 0.5f, 0.5f, 0.5f, 1.0f }});
    V({{  50.0f, 0,  50.0f }, { 0,1,0 }, { 0.5f, 0.5f, 0.5f, 1.0f }});
    V({{  50.0f, 0, -50.0f }, { 0,1,0 }, { 0.5f, 0.5f, 0.5f, 1.0f }});
    uint32_t fb = (uint32_t)(m_floorBaseVertex);
    T(fb, fb+1, fb+2); T(fb, fb+2, fb+3);
    m_floorIndexCount = (UINT)idx.size() - m_floorStartIndex;
    m_floorVertexCount = (UINT)verts.size() - (UINT)m_floorBaseVertex;

    // Triangle
    m_triangleBaseVertex = (INT)verts.size();
    m_triangleStartIndex = (UINT)idx.size();
    {
        uint32_t tb = (uint32_t)verts.size();
        V({{ 0.0f, 0.5f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.2f, 1.0f }});
        V({{ -0.5f, -0.5f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.2f, 1.0f }});
        V({{ 0.5f, -0.5f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.2f, 1.0f }});
        T(tb, tb+1, tb+2);
    }
    m_triangleIndexCount = (UINT)idx.size() - m_triangleStartIndex;
    m_triangleVertexCount = (UINT)verts.size() - (UINT)m_triangleBaseVertex;

    // Sphere
    m_sphereBaseVertex = (INT)verts.size();
    m_sphereStartIndex = (UINT)idx.size();
    {
        const int stacks = 12, slices = 16;
        for (int st = 0; st <= stacks; st++) {
            float phi = XM_PI * st / stacks;
            for (int sl = 0; sl <= slices; sl++) {
                float theta = 2.0f * XM_PI * sl / slices;
                float x = sinf(phi) * cosf(theta);
                float y = cosf(phi);
                float z = sinf(phi) * sinf(theta);
                V({{ x * 0.5f, y * 0.5f, z * 0.5f }, { x, y, z }, { 0.75f, 0.75f, (float)sl/slices, (float)st/stacks }});
            }
        }
        for (int st = 0; st < stacks; st++) {
            for (int sl = 0; sl < slices; sl++) {
                uint32_t a = (uint32_t)(m_sphereBaseVertex + st * (slices + 1) + sl);
                uint32_t b = a + (slices + 1);
                T(a, b, a + 1);
                T(a + 1, b, b + 1);
            }
        }
    }
    m_sphereIndexCount = (UINT)idx.size() - m_sphereStartIndex;
    m_sphereVertexCount = (UINT)verts.size() - (UINT)m_sphereBaseVertex;

    BuildMeshlets(verts, idx, m_cpuMeshlets);
    m_meshletCount = (uint32_t)m_cpuMeshlets.size();

    // Create Metal Renderer Backend
    m_metalBackend = std::make_unique<EngineRender::MetalRendererBackend>();
    if (!m_metalBackend->Initialize(hwnd, m_width, m_height, camera)) {
        return false;
    }

    // Upload static scene geometry (floor, cube, sphere, etc.) to GPU
    m_metalBackend->UploadStaticGeometry(verts, idx);

    m_textureStreamer = std::make_unique<TextureStreamer>(this);
    return true;
}

void Renderer::Destroy() {
    if (m_metalBackend) {
        m_metalBackend->Destroy();
        m_metalBackend.reset();
    }
    m_textureStreamer.reset();
}

void Renderer::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    m_renderWidth = width;
    m_renderHeight = height;

    if (m_metalBackend) {
        m_metalBackend->Resize(width, height);
    }
}

void Renderer::BeginUI() {
    if (m_metalBackend) {
        m_metalBackend->BeginUI();
    }
}

void Renderer::Render(float dt) {
    if (!m_metalBackend) return;

    m_lastDt = dt;
    ProcessLoadedTextures();

    // Update Sun
    UpdateSunFromAngles();

    // Forward settings to Metal Backend
    m_metalBackend->SetUpscaleTechnique((EngineRender::UpscaleTechnique)(int)m_upscaleTechnique);
    m_metalBackend->SetUpscaleQuality((EngineRender::UpscaleQuality)(int)m_upscaleQuality);
    m_metalBackend->SetUpscaleSharpness(m_upscaleSharpness);
    m_metalBackend->SetRayTracingEnabled(m_dxrEnabled);
    m_metalBackend->SetVSyncEnabled(m_vsyncEnabled);
    m_metalBackend->SetAnisotropy(m_anisotropy);

    m_metalBackend->Render(dt);
}

bool Renderer::IsDeviceRemoved() const {
    return false;
}

uint32_t Renderer::CreateDynamicClothModel(const std::string& name, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, const PBRMaterial& material) {
    if (!m_metalBackend || vertices.empty() || indices.empty()) return 0;

    RenderDynamicModel dm;
    dm.name = name;
    dm.filepath = "";
    dm.indexCount = (UINT)indices.size();
    dm.startIndexLocation = 0;
    dm.baseVertexLocation = 0;
    dm.material = material;
    dm.modelId = RenderModel::NextId();
    dm.hasCollision = false;
    dm.position = { 0.0f, 0.0f, 0.0f };
    dm.rotation = { 0.0f, 0.0f, 0.0f };
    dm.scale = { 1.0f, 1.0f, 1.0f };
    XMStoreFloat4x4(&dm.worldMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&dm.previousWorldMatrix, XMMatrixIdentity());

    uint32_t createdId = m_metalBackend->CreateDynamicModel(name, vertices, indices, material);
    dm.modelId = createdId;

    m_dynamicModels.push_back(std::move(dm));
    return createdId;
}

bool Renderer::UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices) {
    if (!m_metalBackend) return false;
    return m_metalBackend->UpdateDynamicModelVertexBuffer(modelId, vertices);
}

bool Renderer::UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (!m_metalBackend) return false;
    return m_metalBackend->UpdateDynamicModelTopology(modelId, vertices, indices);
}

bool Renderer::RemoveDynamicModel(uint32_t modelId) {
    if (m_metalBackend) {
        m_metalBackend->RemoveDynamicModel(modelId);
    }
    for (auto it = m_dynamicModels.begin(); it != m_dynamicModels.end(); ++it) {
        if (it->modelId == modelId) {
            m_dynamicModels.erase(it);
            return true;
        }
    }
    return false;
}

void Renderer::SpawnCube(const XMFLOAT3& pos) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cube %zu", m_models.size());
    RenderModel m(buf, m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount);
    m.position = pos;
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
    XMStoreFloat4x4(&m.worldMatrix, T);
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}

void Renderer::SpawnPlane(const XMFLOAT3& pos) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Plane %zu", m_models.size());
    RenderModel m(buf, m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount);
    m.position = pos;
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
    XMStoreFloat4x4(&m.worldMatrix, T);
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}

void Renderer::SpawnSphere(const XMFLOAT3& pos) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Sphere %zu", m_models.size());
    RenderModel m(buf, m_sphereIndexCount, m_sphereStartIndex, m_sphereBaseVertex, m_sphereVertexCount);
    m.position = pos;
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
    XMStoreFloat4x4(&m.worldMatrix, T);
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}

void Renderer::SpawnTriangle(const XMFLOAT3& pos) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Triangle %zu", m_models.size());
    RenderModel m(buf, m_triangleIndexCount, m_triangleStartIndex, m_triangleBaseVertex, m_triangleVertexCount);
    m.position = pos;
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
    XMStoreFloat4x4(&m.worldMatrix, T);
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}

void Renderer::SpawnDynamicRenderModel(const char* filepath, const XMFLOAT3& pos) {
    if (!filepath) return;
    RenderDynamicModel dm;
    dm.name = filepath;
    dm.filepath = filepath;
    dm.position = pos;
    XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
    XMStoreFloat4x4(&dm.worldMatrix, T);
    m_dynamicModels.push_back(dm);
}

#endif // !defined(_WIN32)
