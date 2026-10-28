#include "Renderer/Renderer.h"

#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

#include "Renderer/Camera.h"
#include "Renderer/PS4/PS4RendererBackend.h"
#include "Renderer/TextureStreamer.h"
#include "Renderer/Texture.h"
#include "Storage/Crypto.h"
#include "Storage/PakCompression.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iostream>

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
        "/app0/Contents/",
        "/app0/",
        "/data/MyEngine/Contents/",
        "/data/MyEngine/",
        "Contents/",
        ""
    };

    for (const auto& base : basePaths) {
        for (int i = 1; i <= 10; ++i) {
            char pakPath[512];
            snprintf(pakPath, sizeof(pakPath), "%scontent%02d.pak", base.c_str(), i);

            std::ifstream file(pakPath, std::ios::binary);
            if (!file.is_open()) continue;

            uint32_t magicOrNumFiles = 0;
            file.read((char*)&magicOrNumFiles, 4);

            if (magicOrNumFiles == 0x4B415045) { // 'EPAK'
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
                if (!PakCrypto::DecryptAES256CBC(masterKey.data(), masterKey.size(),
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
                        if (!PakCrypto::DecryptAES256CBC(masterKey.data(), masterKey.size(),
                                                         fileIV, 16,
                                                         cipherData.data(), cipherData.size(),
                                                         decryptedData)) {
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
    if (m_ps4Backend) {
        m_ps4Backend->SetAnisotropy(aniso);
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

    // 2. Plane (Quad)
    m_planeBaseVertex = (INT)verts.size();
    m_planeStartIndex = (UINT)idx.size();
    {
        uint32_t pb = (uint32_t)(verts.size() - m_planeBaseVertex);
        verts.push_back({{ -0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 0.7f, 0.7f, 0.7f, 1.0f }});
        verts.push_back({{ -0.5f, 0.0f,  0.5f }, { 0, 1, 0 }, { 0.7f, 0.7f, 0.7f, 1.0f }});
        verts.push_back({{  0.5f, 0.0f,  0.5f }, { 0, 1, 0 }, { 0.7f, 0.7f, 0.7f, 1.0f }});
        verts.push_back({{  0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 0.7f, 0.7f, 0.7f, 1.0f }});
        idx.push_back(pb); idx.push_back(pb + 1); idx.push_back(pb + 2);
        idx.push_back(pb); idx.push_back(pb + 2); idx.push_back(pb + 3);
    }
    m_planeIndexCount = (UINT)idx.size() - m_planeStartIndex;
    m_planeVertexCount = (UINT)verts.size() - (UINT)m_planeBaseVertex;

    // 2b. Sphere
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
                V({{ x * 0.5f, y * 0.5f, z * 0.5f }, { x, y, z }, { 0.75f, 0.75f, 0.75f, 1.0f }});
            }
        }
        for (int st = 0; st < stacks; st++) {
            for (int sl = 0; sl < slices; sl++) {
                uint32_t a = (uint32_t)(st * (slices + 1) + sl);
                uint32_t b = a + (slices + 1);
                T(a, b, a + 1);
                T(a + 1, b, b + 1);
            }
        }
    }
    m_sphereIndexCount = (UINT)idx.size() - m_sphereStartIndex;
    m_sphereVertexCount = (UINT)verts.size() - (UINT)m_sphereBaseVertex;

    // 2c. Triangle
    m_triangleBaseVertex = (INT)verts.size();
    m_triangleStartIndex = (UINT)idx.size();
    {
        uint32_t tb = (uint32_t)(verts.size() - m_triangleBaseVertex);
        verts.push_back({{ -0.5f, 0.0f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.8f, 1.0f }});
        verts.push_back({{  0.5f, 0.0f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.8f, 1.0f }});
        verts.push_back({{  0.0f, 1.0f, 0.0f }, { 0, 0, 1 }, { 0.8f, 0.8f, 0.8f, 1.0f }});
        idx.push_back(tb); idx.push_back(tb + 1); idx.push_back(tb + 2);
    }
    m_triangleIndexCount = (UINT)idx.size() - m_triangleStartIndex;
    m_triangleVertexCount = (UINT)verts.size() - (UINT)m_triangleBaseVertex;

    // 3. House Walls (4 separate faces with flat outward normals)
    m_wallBaseVertex = (INT)verts.size();
    m_wallStartIndex = (UINT)idx.size();
    UINT wallVBase = (UINT)m_wallBaseVertex;
    UINT wallIdxStart = m_wallStartIndex;
    float bx = 3.0f; // 6m wide house
    float bz = 3.5f; // 7m deep house
    float bh = 3.5f; // 3.5m wall height
    XMFLOAT4 wallCol = { 0.88f, 0.85f, 0.80f, 1.0f };

    // Front Wall (+Z face)
    {
        uint32_t b = (uint32_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 0.0f, 0.0f, 1.0f };
        V({{ -bx, 0.0f, bz }, n, wallCol });
        V({{  bx, 0.0f, bz }, n, wallCol });
        V({{  bx,   bh, bz }, n, wallCol });
        V({{ -bx,   bh, bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Back Wall (-Z face)
    {
        uint32_t b = (uint32_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 0.0f, 0.0f, -1.0f };
        V({{  bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx,   bh, -bz }, n, wallCol });
        V({{  bx,   bh, -bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Left Wall (-X face)
    {
        uint32_t b = (uint32_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { -1.0f, 0.0f, 0.0f };
        V({{ -bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx, 0.0f,  bz }, n, wallCol });
        V({{ -bx,   bh,  bz }, n, wallCol });
        V({{ -bx,   bh, -bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Right Wall (+X face)
    {
        uint32_t b = (uint32_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 1.0f, 0.0f, 0.0f };
        V({{  bx, 0.0f,  bz }, n, wallCol });
        V({{  bx, 0.0f, -bz }, n, wallCol });
        V({{  bx,   bh, -bz }, n, wallCol });
        V({{  bx,   bh,  bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    m_wallIndexCount = (UINT)idx.size() - wallIdxStart;
    m_wallVertexCount = (UINT)verts.size() - wallVBase;

    // 4. House Roof (4 separate triangular faces with true outward flat normals)
    m_roofBaseVertex = (INT)verts.size();
    m_roofStartIndex = (UINT)idx.size();
    UINT roofVBase = (UINT)m_roofBaseVertex;
    UINT roofIdxStart = m_roofStartIndex;
    float rx = bx + 0.4f; // Overhang
    float rz = bz + 0.4f;
    float rBaseY = bh - 0.05f;
    float rPeakY = bh + 2.2f;
    XMFLOAT4 roofCol = { 0.75f, 0.28f, 0.18f, 1.0f }; // Warm terracotta roof tiles

    auto addRoofFace = [&](XMFLOAT3 v0, XMFLOAT3 v1, XMFLOAT3 v2) {
        uint32_t b = (uint32_t)(verts.size() - roofVBase);
        XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&v1), XMLoadFloat3(&v0));
        XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&v2), XMLoadFloat3(&v0));
        XMVECTOR nVec = XMVector3Normalize(XMVector3Cross(e1, e2));
        XMFLOAT3 n; XMStoreFloat3(&n, nVec);
        V({ v0, n, roofCol });
        V({ v1, n, roofCol });
        V({ v2, n, roofCol });
        T(b, b+1, b+2);
    };

    // Front Slope (+Z)
    addRoofFace({ -rx, rBaseY, rz }, { rx, rBaseY, rz }, { 0.0f, rPeakY, 0.0f });
    // Back Slope (-Z)
    addRoofFace({ rx, rBaseY, -rz }, { -rx, rBaseY, -rz }, { 0.0f, rPeakY, 0.0f });
    // Left Slope (-X)
    addRoofFace({ -rx, rBaseY, -rz }, { -rx, rBaseY, rz }, { 0.0f, rPeakY, 0.0f });
    // Right Slope (+X)
    addRoofFace({ rx, rBaseY, rz }, { rx, rBaseY, -rz }, { 0.0f, rPeakY, 0.0f });

    m_roofIndexCount = (UINT)idx.size() - roofIdxStart;
    m_roofVertexCount = (UINT)verts.size() - roofVBase;

    // 5. Subdivided Floor Grid (30x30 tiles, 60m x 60m, centered around camera view)
    m_floorBaseVertex = (INT)verts.size();
    m_floorStartIndex = (UINT)idx.size();
    {
        const float tileSize = 3.5f;
        const int minX = -8; // X from -28 to +28
        const int maxX = 8;
        const int minZ = -4; // Z from -14 to +42 (camera starts at Z = -5)
        const int maxZ = 12;

        for (int gz = minZ; gz < maxZ; ++gz) {
            for (int gx = minX; gx < maxX; ++gx) {
                float x0 = gx * tileSize;
                float x1 = (gx + 1) * tileSize;
                float z0 = gz * tileSize;
                float z1 = (gz + 1) * tileSize;

                bool isEven = ((gx + gz) & 1) == 0;
                XMFLOAT4 col = isEven 
                    ? XMFLOAT4(0.58f, 0.60f, 0.64f, 1.0f)   // Light concrete
                    : XMFLOAT4(0.35f, 0.38f, 0.42f, 1.0f);  // Dark slate

                uint32_t b = (uint32_t)(verts.size() - m_floorBaseVertex);
                verts.push_back({{ x0, 0.0f, z0 }, { 0, 1, 0 }, col });
                verts.push_back({{ x0, 0.0f, z1 }, { 0, 1, 0 }, col });
                verts.push_back({{ x1, 0.0f, z1 }, { 0, 1, 0 }, col });
                verts.push_back({{ x1, 0.0f, z0 }, { 0, 1, 0 }, col });

                idx.push_back(b);     idx.push_back(b + 1); idx.push_back(b + 2);
                idx.push_back(b);     idx.push_back(b + 2); idx.push_back(b + 3);
            }
        }
    }
    m_floorIndexCount = (UINT)idx.size() - m_floorStartIndex;
    m_floorVertexCount = (UINT)verts.size() - (UINT)m_floorBaseVertex;

    // Default models are populated dynamically from scene_save.txt in Application::LoadScene()
    m_models.clear();

    // Create PS4 Renderer Backend
    m_ps4Backend = std::make_unique<EngineRender::PS4RendererBackend>();
    if (!m_ps4Backend->Initialize(hwnd, m_width, m_height, camera)) {
        return false;
    }
    if (hwnd) {
        m_ps4Backend->SetPS4Window(static_cast<EnginePlatform::PS4Window*>(hwnd));
    }

    // Upload static scene geometry to PS4 backend
    m_ps4Backend->UploadStaticGeometry(verts, idx);

    m_textureStreamer = std::make_unique<TextureStreamer>(this);
    std::cout << "[Renderer_PS4] Initialized successfully" << std::endl;
    return true;
}

void Renderer::Destroy() {
    if (m_ps4Backend) {
        m_ps4Backend->Destroy();
        m_ps4Backend.reset();
    }
    m_textureStreamer.reset();
}

void Renderer::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    m_renderWidth = width;
    m_renderHeight = height;

    if (m_ps4Backend) {
        m_ps4Backend->Resize(width, height);
    }
}

void Renderer::BeginUI() {
    if (m_ps4Backend) {
        m_ps4Backend->BeginUI();
    }
}

void Renderer::Render(float dt) {
    if (!m_ps4Backend) return;

    m_lastDt = dt;
    ProcessLoadedTextures();
    UpdateSunFromAngles();

    // Rotate showcase CenterCube
    for (auto& m : m_models) {
        if (m.name == "CenterCube") {
            m.rotation.y += dt * 35.0f;
            if (m.rotation.y >= 360.0f) m.rotation.y -= 360.0f;
        }
    }

    // Advance cloud simulation time
    m_cloudTime += dt;

    // Sync lighting and atmospheric environment with PS4 backend
    m_ps4Backend->SetLightDir(m_constantBufferData.lightDir);
    m_ps4Backend->SetLightColor(m_constantBufferData.lightColor);
    m_ps4Backend->SetSkyColor(m_skyColor);
    m_ps4Backend->SetCloudDensity(m_cloudDensity);
    m_ps4Backend->SetCloudTime(m_cloudTime);
    m_ps4Backend->SetRainIntensity(m_rainIntensity);

    // Sync scene models
    std::vector<EngineRender::SceneDrawItem> drawItems;
    drawItems.reserve(m_models.size());
    for (const auto& model : m_models) {
        if (!model.visible || model.indexCount == 0) continue;
        EngineRender::SceneDrawItem item;
        item.startIndex = model.startIndexLocation;
        item.indexCount = model.indexCount;
        item.baseVertex = model.baseVertexLocation;
        item.material = model.material;
        item.visible = model.visible;

        XMMATRIX S = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
        XMMATRIX R = XMMatrixRotationRollPitchYaw(
            DirectX::XMConvertToRadians(model.rotation.x),
            DirectX::XMConvertToRadians(model.rotation.y),
            DirectX::XMConvertToRadians(model.rotation.z)
        );
        XMMATRIX T = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
        XMStoreFloat4x4(&item.worldMatrix, S * R * T);

        drawItems.push_back(item);
    }
    m_ps4Backend->SetSceneDrawItems(std::move(drawItems));

    m_ps4Backend->SetUpscaleTechnique((EngineRender::UpscaleTechnique)(int)m_upscaleTechnique);
    m_ps4Backend->SetUpscaleQuality((EngineRender::UpscaleQuality)(int)m_upscaleQuality);
    m_ps4Backend->SetUpscaleSharpness(m_upscaleSharpness);
    m_ps4Backend->SetVSyncEnabled(m_vsyncEnabled);
    m_ps4Backend->SetAnisotropy(m_anisotropy);

    m_ps4Backend->Render(dt);
}

bool Renderer::IsDeviceRemoved() const {
    return false;
}

uint32_t Renderer::CreateDynamicClothModel(const std::string& name, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, const PBRMaterial& material) {
    if (!m_ps4Backend || vertices.empty() || indices.empty()) return 0;

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

    uint32_t createdId = m_ps4Backend->CreateDynamicModel(name, vertices, indices, material);
    dm.modelId = createdId;

    m_dynamicModels.push_back(std::move(dm));
    return createdId;
}

bool Renderer::UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices) {
    if (!m_ps4Backend) return false;
    return m_ps4Backend->UpdateDynamicModelVertexBuffer(modelId, vertices);
}

bool Renderer::UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (!m_ps4Backend) return false;
    return m_ps4Backend->UpdateDynamicModelTopology(modelId, vertices, indices);
}

bool Renderer::RemoveDynamicModel(uint32_t modelId) {
    if (m_ps4Backend) {
        m_ps4Backend->RemoveDynamicModel(modelId);
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

void Renderer::InitPortalVisuals() {}
void Renderer::UpdatePortalVisuals() {}

void Renderer::SetXeLLEnabled(bool enabled, bool boost) {
    (void)enabled;
    (void)boost;
}

#endif // defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
