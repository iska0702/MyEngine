#include "TerrainSystem.h"
#if defined(_WIN32)
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineTerrain {

static const char* g_TerrainShaderHLSL = R"(
cbuffer TerrainCB : register(b0) {
    float4x4 g_ViewProj;
    float4   g_LightDir;
    float4   g_LightColor;
    float4   g_CameraPosAndTime;
    float4   g_WindParams;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 splat    : TEXCOORD1;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 splat    : TEXCOORD1;
};

PSInput VSTerrain(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), g_ViewProj);
    output.worldPos = input.position;
    output.normal   = input.normal;
    output.uv       = input.uv;
    output.splat    = input.splat;
    return output;
}

float4 PSTerrain(PSInput input) : SV_TARGET {
    float3 grassColor = float3(0.24, 0.42, 0.16);
    float3 dirtColor  = float3(0.40, 0.28, 0.18);
    float3 rockColor  = float3(0.35, 0.35, 0.38);
    float3 snowColor  = float3(0.92, 0.95, 0.98);

    float3 baseAlbedo = grassColor * input.splat.x +
                        dirtColor  * input.splat.y +
                        rockColor  * input.splat.z +
                        snowColor  * input.splat.w;

    float3 N = normalize(input.normal);
    float3 L = normalize(-g_LightDir.xyz);
    float NdotL = saturate(dot(N, L));
    float ambient = 0.25;
    float3 lighting = baseAlbedo * (g_LightColor.rgb * NdotL + ambient);

    return float4(lighting, 1.0);
}

struct VSFoliageInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

struct PSFoliageInput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

PSFoliageInput VSFoliage(VSFoliageInput input) {
    PSFoliageInput output;
    float3 worldPos = input.position;

    float windFactor = saturate(1.0 - input.uv.y);
    float windTime = g_WindParams.z;
    float windSpeed = g_WindParams.x;
    float windStrength = g_WindParams.y;
    float wave = sin(windTime * windSpeed + worldPos.x * 0.7 + worldPos.z * 0.5);
    worldPos.x += wave * windStrength * windFactor;
    worldPos.z += wave * windStrength * 0.6 * windFactor;

    output.position = mul(float4(worldPos, 1.0), g_ViewProj);
    output.uv       = input.uv;
    output.color    = input.color;
    return output;
}

float4 PSFoliage(PSFoliageInput input) : SV_TARGET {
    float2 uv = input.uv;
    float shape = 1.0 - abs(uv.x - 0.5) * 2.0;
    clip(shape - uv.y * 0.7 - 0.12);

    float3 grassGreen = input.color.rgb * lerp(0.65, 1.1, 1.0 - uv.y);
    return float4(grassGreen, 1.0);
}
)";

TerrainSystem& TerrainSystem::Get() {
    static TerrainSystem instance;
    return instance;
}

TerrainSystem::TerrainSystem() {
    InitNoise(m_config.seed);
}

TerrainSystem::~TerrainSystem() {
    Shutdown();
}

void TerrainSystem::InitNoise(int seed) {
    std::mt19937 g(seed);
    std::vector<int> p(256);
    std::iota(p.begin(), p.end(), 0);
    std::shuffle(p.begin(), p.end(), g);
    for (int i = 0; i < 256; ++i) {
        m_perm[i] = p[i];
        m_perm[256 + i] = p[i];
    }
}

static float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static float Grad(int hash, float x, float z) {
    int h = hash & 7;
    float u = h < 4 ? x : z;
    float v = h < 4 ? z : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float TerrainSystem::PerlinNoise2D(float x, float z) const {
    int X = static_cast<int>(std::floor(x)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    z -= std::floor(z);

    float u = Fade(x);
    float v = Fade(z);

    int A = m_perm[X] + Z;
    int B = m_perm[X + 1] + Z;

    return std::lerp(
        std::lerp(Grad(m_perm[A], x, z), Grad(m_perm[B], x - 1.0f, z), u),
        std::lerp(Grad(m_perm[A + 1], x, z - 1.0f), Grad(m_perm[B + 1], x - 1.0f, z - 1.0f), u),
        v
    );
}

float TerrainSystem::SampleFBM(float x, float z, int octaves, float persistence, float lacunarity) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += PerlinNoise2D(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return total / maxValue;
}

bool TerrainSystem::Initialize(ID3D12Device* device) {
    if (m_initialized) return true;
    m_device = device;
    if (!m_device) return false;

    if (!CreatePipelines() || !CreateBuffers()) {
        return false;
    }

    // Generate initial terrain and foliage
    Generate(m_config);

    m_initialized = true;
    return true;
}

void TerrainSystem::Shutdown() {
    if (m_constantBuffer && m_mappedCbv) {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedCbv = nullptr;
    }
    m_initialized = false;
}

bool TerrainSystem::CreatePipelines() {
    // 1. Root Signature
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob))) {
        return false;
    }
    if (FAILED(m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)))) {
        return false;
    }

    // 2. Compile Shaders
    ComPtr<ID3DBlob> vsTerrain, psTerrain, vsFoliage, psFoliage;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    D3DCompile(g_TerrainShaderHLSL, strlen(g_TerrainShaderHLSL), "Terrain", nullptr, nullptr, "VSTerrain", "vs_5_1", flags, 0, &vsTerrain, nullptr);
    D3DCompile(g_TerrainShaderHLSL, strlen(g_TerrainShaderHLSL), "Terrain", nullptr, nullptr, "PSTerrain", "ps_5_1", flags, 0, &psTerrain, nullptr);
    D3DCompile(g_TerrainShaderHLSL, strlen(g_TerrainShaderHLSL), "Foliage", nullptr, nullptr, "VSFoliage", "vs_5_1", flags, 0, &vsFoliage, nullptr);
    D3DCompile(g_TerrainShaderHLSL, strlen(g_TerrainShaderHLSL), "Foliage", nullptr, nullptr, "PSFoliage", "ps_5_1", flags, 0, &psFoliage, nullptr);

    if (!vsTerrain || !psTerrain || !vsFoliage || !psFoliage) return false;

    // 3. Terrain Input Layout
    D3D12_INPUT_ELEMENT_DESC terrainLayout[] = {
        { "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",  1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Terrain PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsTerrain->GetBufferPointer(), vsTerrain->GetBufferSize() };
    psoDesc.PS = { psTerrain->GetBufferPointer(), psTerrain->GetBufferSize() };
    psoDesc.InputLayout = { terrainLayout, _countof(terrainLayout) };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_terrainPSO)))) {
        return false;
    }

    // Foliage Input Layout & PSO
    D3D12_INPUT_ELEMENT_DESC foliageLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    psoDesc.VS = { vsFoliage->GetBufferPointer(), vsFoliage->GetBufferSize() };
    psoDesc.PS = { psFoliage->GetBufferPointer(), psFoliage->GetBufferSize() };
    psoDesc.InputLayout = { foliageLayout, _countof(foliageLayout) };
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // Double sided for grass

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_foliagePSO)))) {
        return false;
    }

    return true;
}

bool TerrainSystem::CreateBuffers() {
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = 256;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_constantBuffer)))) {
        return false;
    }
    m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedCbv));
    return true;
}

void TerrainSystem::Generate(const TerrainConfig& config) {
    m_config = config;
    InitNoise(m_config.seed);

    int vxCount = m_config.width + 1;
    int vzCount = m_config.length + 1;
    m_heightmap.resize(vxCount * vzCount);
    m_vertices.resize(vxCount * vzCount);

    float halfW = (m_config.width * m_config.cellSize) * 0.5f;
    float halfL = (m_config.length * m_config.cellSize) * 0.5f;

    // 1. Generate Heightmap
    for (int z = 0; z < vzCount; ++z) {
        for (int x = 0; x < vxCount; ++x) {
            float worldX = -halfW + x * m_config.cellSize;
            float worldZ = -halfL + z * m_config.cellSize;

            float noiseVal = SampleFBM(worldX * m_config.noiseScale, worldZ * m_config.noiseScale,
                                       m_config.octaves, m_config.persistence, m_config.lacunarity);
            // Shift range from [-1, 1] to [0, maxHeight]
            float h = (noiseVal * 0.5f + 0.5f) * m_config.maxHeight;

            int idx = z * vxCount + x;
            m_heightmap[idx] = h;
        }
    }

    // 2. Compute Normals and Splat Weights
    for (int z = 0; z < vzCount; ++z) {
        for (int x = 0; x < vxCount; ++x) {
            int idx = z * vxCount + x;
            float worldX = -halfW + x * m_config.cellSize;
            float worldZ = -halfL + z * m_config.cellSize;
            float h = m_heightmap[idx];

            // Central differences for smooth normal
            float hL = (x > 0) ? m_heightmap[z * vxCount + (x - 1)] : h;
            float hR = (x < vxCount - 1) ? m_heightmap[z * vxCount + (x + 1)] : h;
            float hD = (z > 0) ? m_heightmap[(z - 1) * vxCount + x] : h;
            float hU = (z < vzCount - 1) ? m_heightmap[(z + 1) * vxCount + x] : h;

            XMVECTOR normal = XMVector3Normalize(XMVectorSet(hL - hR, 2.0f * m_config.cellSize, hD - hU, 0.0f));
            XMFLOAT3 normFloat;
            XMStoreFloat3(&normFloat, normal);

            TerrainVertex& vert = m_vertices[idx];
            vert.position = XMFLOAT3(worldX, h, worldZ);
            vert.normal = normFloat;
            vert.uv = XMFLOAT2(static_cast<float>(x) / m_config.width * 16.0f,
                               static_cast<float>(z) / m_config.length * 16.0f);

            // Splat weights: x = grass, y = dirt, z = rock, w = snow
            float slope = 1.0f - normFloat.y;
            float snow = (h > m_config.snowHeightThreshold) ? std::clamp((h - m_config.snowHeightThreshold) / 2.5f, 0.0f, 1.0f) : 0.0f;
            float rock = (slope > m_config.rockSlopeThreshold) ? std::clamp((slope - m_config.rockSlopeThreshold) / 0.25f, 0.0f, 1.0f) : 0.0f;
            float grass = std::clamp(1.0f - rock - snow, 0.0f, 1.0f);
            float dirt  = (slope > 0.15f && slope <= m_config.rockSlopeThreshold) ? 0.35f : 0.0f;

            vert.splat = XMFLOAT4(grass, dirt, rock, snow);
        }
    }

    // 3. Generate Indices
    m_indices.clear();
    m_indices.reserve(m_config.width * m_config.length * 6);
    for (int z = 0; z < m_config.length; ++z) {
        for (int x = 0; x < m_config.width; ++x) {
            uint32_t topLeft     = z * vxCount + x;
            uint32_t topRight    = topLeft + 1;
            uint32_t bottomLeft  = (z + 1) * vxCount + x;
            uint32_t bottomRight = bottomLeft + 1;

            m_indices.push_back(topLeft);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(topRight);

            m_indices.push_back(topRight);
            m_indices.push_back(bottomLeft);
            m_indices.push_back(bottomRight);
        }
    }

    UploadTerrainGeometry();
    GenerateFoliage(m_foliageConfig);
}

void TerrainSystem::GenerateFoliage(const FoliageConfig& config) {
    m_foliageConfig = config;
    if (!m_foliageConfig.enabled) {
        m_foliageVertices.clear();
        m_foliageIndices.clear();
        return;
    }

    std::mt19937 rng(m_config.seed + 42);
    std::uniform_real_distribution<float> distPos(-0.5f, 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, 3.14159f);
    std::uniform_real_distribution<float> distScale(0.75f, 1.35f);
    std::uniform_real_distribution<float> distPhase(0.0f, 6.28318f);

    float halfW = (m_config.width * m_config.cellSize) * 0.5f;
    float halfL = (m_config.length * m_config.cellSize) * 0.5f;

    m_foliageVertices.clear();
    m_foliageIndices.clear();

    size_t count = 0;
    size_t maxAttempts = m_foliageConfig.instanceCount * 3;

    for (size_t attempt = 0; attempt < maxAttempts && count < m_foliageConfig.instanceCount; ++attempt) {
        float wx = distPos(rng) * (m_config.width * m_config.cellSize * 0.9f);
        float wz = distPos(rng) * (m_config.length * m_config.cellSize * 0.9f);

        float h = GetHeightAt(wx, wz);
        XMFLOAT3 norm = GetNormalAt(wx, wz);

        // Only place on gentle grass slopes below snow line
        if (norm.y < 0.85f || h > m_config.snowHeightThreshold - 1.0f) continue;

        float rot = distRot(rng);
        float scale = distScale(rng);
        float phase = distPhase(rng);

        float hw = m_foliageConfig.grassWidth * 0.5f * scale;
        float hh = m_foliageConfig.grassHeight * scale;

        // Cross-quad billboard (2 crossing planes)
        for (int plane = 0; plane < 2; ++plane) {
            float angle = rot + (plane * 1.5707963f);
            float dx = std::cos(angle) * hw;
            float dz = std::sin(angle) * hw;

            uint32_t baseIdx = static_cast<uint32_t>(m_foliageVertices.size());

            FoliageVertex v0, v1, v2, v3;
            v0.position = XMFLOAT3(wx - dx, h, wz - dz);
            v0.normal   = norm;
            v0.uv       = XMFLOAT2(0.0f, 1.0f);
            v0.color    = XMFLOAT4(0.35f, 0.65f, 0.22f, 1.0f);

            v1.position = XMFLOAT3(wx - dx, h + hh, wz - dz);
            v1.normal   = norm;
            v1.uv       = XMFLOAT2(0.0f, 0.0f);
            v1.color    = XMFLOAT4(0.45f, 0.82f, 0.30f, 1.0f);

            v2.position = XMFLOAT3(wx + dx, h + hh, wz + dz);
            v2.normal   = norm;
            v2.uv       = XMFLOAT2(1.0f, 0.0f);
            v2.color    = XMFLOAT4(0.45f, 0.82f, 0.30f, 1.0f);

            v3.position = XMFLOAT3(wx + dx, h, wz + dz);
            v3.normal   = norm;
            v3.uv       = XMFLOAT2(1.0f, 1.0f);
            v3.color    = XMFLOAT4(0.35f, 0.65f, 0.22f, 1.0f);

            m_foliageVertices.push_back(v0);
            m_foliageVertices.push_back(v1);
            m_foliageVertices.push_back(v2);
            m_foliageVertices.push_back(v3);

            m_foliageIndices.push_back(baseIdx + 0);
            m_foliageIndices.push_back(baseIdx + 1);
            m_foliageIndices.push_back(baseIdx + 2);
            m_foliageIndices.push_back(baseIdx + 0);
            m_foliageIndices.push_back(baseIdx + 2);
            m_foliageIndices.push_back(baseIdx + 3);
        }
        count++;
    }

    UploadFoliageGeometry();
}

void TerrainSystem::UploadTerrainGeometry() {
    if (!m_device || m_vertices.empty() || m_indices.empty()) return;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    // VB
    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(TerrainVertex) * m_vertices.size();
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                      IID_PPV_ARGS(&m_terrainVB));
    void* mappedVb = nullptr;
    m_terrainVB->Map(0, nullptr, &mappedVb);
    memcpy(mappedVb, m_vertices.data(), vbDesc.Width);
    m_terrainVB->Unmap(0, nullptr);

    m_terrainVBView.BufferLocation = m_terrainVB->GetGPUVirtualAddress();
    m_terrainVBView.StrideInBytes = sizeof(TerrainVertex);
    m_terrainVBView.SizeInBytes = static_cast<UINT>(vbDesc.Width);

    // IB
    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = sizeof(uint32_t) * m_indices.size();

    m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                      IID_PPV_ARGS(&m_terrainIB));
    void* mappedIb = nullptr;
    m_terrainIB->Map(0, nullptr, &mappedIb);
    memcpy(mappedIb, m_indices.data(), ibDesc.Width);
    m_terrainIB->Unmap(0, nullptr);

    m_terrainIBView.BufferLocation = m_terrainIB->GetGPUVirtualAddress();
    m_terrainIBView.Format = DXGI_FORMAT_R32_UINT;
    m_terrainIBView.SizeInBytes = static_cast<UINT>(ibDesc.Width);
}

void TerrainSystem::UploadFoliageGeometry() {
    if (!m_device || m_foliageVertices.empty() || m_foliageIndices.empty()) return;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(FoliageVertex) * m_foliageVertices.size();
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                      IID_PPV_ARGS(&m_foliageVB));
    void* mappedVb = nullptr;
    m_foliageVB->Map(0, nullptr, &mappedVb);
    memcpy(mappedVb, m_foliageVertices.data(), vbDesc.Width);
    m_foliageVB->Unmap(0, nullptr);

    m_foliageVBView.BufferLocation = m_foliageVB->GetGPUVirtualAddress();
    m_foliageVBView.StrideInBytes = sizeof(FoliageVertex);
    m_foliageVBView.SizeInBytes = static_cast<UINT>(vbDesc.Width);

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = sizeof(uint32_t) * m_foliageIndices.size();

    m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                      IID_PPV_ARGS(&m_foliageIB));
    void* mappedIb = nullptr;
    m_foliageIB->Map(0, nullptr, &mappedIb);
    memcpy(mappedIb, m_foliageIndices.data(), ibDesc.Width);
    m_foliageIB->Unmap(0, nullptr);

    m_foliageIBView.BufferLocation = m_foliageIB->GetGPUVirtualAddress();
    m_foliageIBView.Format = DXGI_FORMAT_R32_UINT;
    m_foliageIBView.SizeInBytes = static_cast<UINT>(ibDesc.Width);
}

float TerrainSystem::GetHeightAt(float worldX, float worldZ) const {
    if (m_heightmap.empty()) return 0.0f;

    float halfW = (m_config.width * m_config.cellSize) * 0.5f;
    float halfL = (m_config.length * m_config.cellSize) * 0.5f;

    float gridX = (worldX + halfW) / m_config.cellSize;
    float gridZ = (worldZ + halfL) / m_config.cellSize;

    if (gridX < 0.0f || gridX >= m_config.width || gridZ < 0.0f || gridZ >= m_config.length) {
        return 0.0f; // Outside terrain bounds
    }

    int x0 = static_cast<int>(std::floor(gridX));
    int z0 = static_cast<int>(std::floor(gridZ));
    int x1 = (std::min)(x0 + 1, m_config.width);
    int z1 = (std::min)(z0 + 1, m_config.length);

    float tx = gridX - x0;
    float tz = gridZ - z0;

    int vxCount = m_config.width + 1;
    float h00 = m_heightmap[z0 * vxCount + x0];
    float h10 = m_heightmap[z0 * vxCount + x1];
    float h01 = m_heightmap[z1 * vxCount + x0];
    float h11 = m_heightmap[z1 * vxCount + x1];

    float h0 = std::lerp(h00, h10, tx);
    float h1 = std::lerp(h01, h11, tx);
    return std::lerp(h0, h1, tz);
}

XMFLOAT3 TerrainSystem::GetNormalAt(float worldX, float worldZ) const {
    float eps = m_config.cellSize * 0.5f;
    float hL = GetHeightAt(worldX - eps, worldZ);
    float hR = GetHeightAt(worldX + eps, worldZ);
    float hD = GetHeightAt(worldX, worldZ - eps);
    float hU = GetHeightAt(worldX, worldZ + eps);

    XMVECTOR N = XMVector3Normalize(XMVectorSet(hL - hR, 2.0f * eps, hD - hU, 0.0f));
    XMFLOAT3 result;
    XMStoreFloat3(&result, N);
    return result;
}

bool TerrainSystem::IsOnTerrain(float worldX, float worldZ) const {
    float halfW = (m_config.width * m_config.cellSize) * 0.5f;
    float halfL = (m_config.length * m_config.cellSize) * 0.5f;
    return (worldX >= -halfW && worldX <= halfW && worldZ >= -halfL && worldZ <= halfL);
}

void TerrainSystem::Update(float dt, const XMFLOAT3& cameraPos) {
    if (!m_config.enabled) return;
    m_windTime += dt;
}

void TerrainSystem::Render(ID3D12GraphicsCommandList* commandList,
                           const XMMATRIX& viewProj,
                           const XMFLOAT3& lightDir,
                           const XMFLOAT3& lightColor,
                           const XMFLOAT3& cameraPos,
                           float time) {
    if (!m_config.enabled || !commandList || !m_terrainVB || !m_terrainIB || !m_mappedCbv) {
        return;
    }

    // Update Constant Buffer
    m_mappedCbv->viewProj = XMMatrixTranspose(viewProj);
    m_mappedCbv->lightDir = XMFLOAT4(lightDir.x, lightDir.y, lightDir.z, 0.0f);
    m_mappedCbv->lightColor = XMFLOAT4(lightColor.x, lightColor.y, lightColor.z, 1.0f);
    m_mappedCbv->cameraPosAndTime = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, time);
    m_mappedCbv->windParams = XMFLOAT4(m_foliageConfig.windSpeed, m_foliageConfig.windStrength, m_windTime, 0.0f);

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 1. Draw Terrain Surface
    commandList->SetPipelineState(m_terrainPSO.Get());
    commandList->IASetVertexBuffers(0, 1, &m_terrainVBView);
    commandList->IASetIndexBuffer(&m_terrainIBView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(m_indices.size()), 1, 0, 0, 0);

    // 2. Draw Foliage
    if (m_foliageConfig.enabled && m_foliageVB && m_foliageIB && !m_foliageIndices.empty()) {
        commandList->SetPipelineState(m_foliagePSO.Get());
        commandList->IASetVertexBuffers(0, 1, &m_foliageVBView);
        commandList->IASetIndexBuffer(&m_foliageIBView);
        commandList->DrawIndexedInstanced(static_cast<UINT>(m_foliageIndices.size()), 1, 0, 0, 0);
    }
}

void TerrainSystem::ApplyPresetRollingHills() {
    m_config.maxHeight = 12.0f;
    m_config.noiseScale = 0.025f;
    m_config.octaves = 4;
    m_config.persistence = 0.45f;
    m_config.rockSlopeThreshold = 0.65f;
    m_config.snowHeightThreshold = 14.0f;
    Generate(m_config);
}

void TerrainSystem::ApplyPresetAlpineMountains() {
    m_config.maxHeight = 26.0f;
    m_config.noiseScale = 0.035f;
    m_config.octaves = 5;
    m_config.persistence = 0.52f;
    m_config.rockSlopeThreshold = 0.45f;
    m_config.snowHeightThreshold = 15.0f;
    Generate(m_config);
}

void TerrainSystem::ApplyPresetPlains() {
    m_config.maxHeight = 4.0f;
    m_config.noiseScale = 0.015f;
    m_config.octaves = 3;
    m_config.persistence = 0.35f;
    m_config.rockSlopeThreshold = 0.8f;
    m_config.snowHeightThreshold = 30.0f;
    Generate(m_config);
}

void TerrainSystem::DrawUI(const XMFLOAT3& cameraPos) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Terrain & Foliage Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Enable Terrain & Foliage Rendering", &m_config.enabled);

    float groundH = GetHeightAt(cameraPos.x, cameraPos.z);
    ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f),
                       "Camera Pos: (%.1f, %.1f, %.1f) | Ground Height: %.2fm",
                       cameraPos.x, cameraPos.y, cameraPos.z, groundH);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Terrain Presets:");
    if (ImGui::Button(" Rolling Hills"))    ApplyPresetRollingHills();
    ImGui::SameLine();
    if (ImGui::Button(" Alpine Mountains")) ApplyPresetAlpineMountains();
    ImGui::SameLine();
    if (ImGui::Button(" Plains"))           ApplyPresetPlains();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Procedural Parameters:");

    bool dirty = false;
    dirty |= ImGui::SliderFloat("Max Height", &m_config.maxHeight, 1.0f, 40.0f, "%.1fm");
    dirty |= ImGui::SliderFloat("Noise Scale", &m_config.noiseScale, 0.005f, 0.08f, "%.4f");
    dirty |= ImGui::SliderInt("Octaves", &m_config.octaves, 1, 6);
    dirty |= ImGui::SliderFloat("Persistence", &m_config.persistence, 0.1f, 0.8f, "%.2f");
    dirty |= ImGui::InputInt("Seed", &m_config.seed);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "Splat Layers:");
    dirty |= ImGui::SliderFloat("Rock Slope Angle", &m_config.rockSlopeThreshold, 0.2f, 0.9f, "%.2f");
    dirty |= ImGui::SliderFloat("Snow Elevation", &m_config.snowHeightThreshold, 5.0f, 35.0f, "%.1fm");

    if (ImGui::Button(" Re-Generate Terrain")) {
        dirty = true;
    }

    if (dirty) {
        Generate(m_config);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Grass & Foliage:");
    ImGui::Checkbox("Foliage Enabled", &m_foliageConfig.enabled);
    ImGui::SliderFloat("Wind Speed", &m_foliageConfig.windSpeed, 0.5f, 8.0f, "%.1f");
    ImGui::SliderFloat("Wind Strength", &m_foliageConfig.windStrength, 0.05f, 1.0f, "%.2f");
    ImGui::Text("Foliage Vertices: %zu | Quads: %zu", m_foliageVertices.size(), m_foliageIndices.size() / 6);

    ImGui::End();
#endif
}

} // namespace EngineTerrain
#endif
