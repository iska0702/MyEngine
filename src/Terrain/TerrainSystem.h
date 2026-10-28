#pragma once

#include "TerrainTypes.h"
#include <vector>
#include <memory>
#include <string>
#if defined(_WIN32)
#include <wrl/client.h>
#include <d3d12.h>
#include <DirectXMath.h>
#else
#include "Platform/DirectXMath/DirectXMath.h"
#endif

namespace EngineTerrain {

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
#endif

class TerrainSystem {
public:
#if defined(_WIN32)
    static TerrainSystem& Get();
    bool Initialize(ID3D12Device* device);
    void Shutdown();

    // Procedural Generation
    void Generate(const TerrainConfig& config);
    void GenerateFoliage(const FoliageConfig& config);

    // Queries & Collision
    float GetHeightAt(float worldX, float worldZ) const;
    XMFLOAT3 GetNormalAt(float worldX, float worldZ) const;
    bool IsOnTerrain(float worldX, float worldZ) const;

    // Simulation & Rendering
    void Update(float dt, const XMFLOAT3& cameraPos);
    void Render(ID3D12GraphicsCommandList* commandList,
                const XMMATRIX& viewProj,
                const XMFLOAT3& lightDir,
                const XMFLOAT3& lightColor,
                const XMFLOAT3& cameraPos,
                float time);

    // Presets
    void ApplyPresetRollingHills();
    void ApplyPresetAlpineMountains();
    void ApplyPresetPlains();

    // Editor UI
    void DrawUI(const XMFLOAT3& cameraPos);
#else
    static TerrainSystem& Get() {
        static TerrainSystem s_instance;
        return s_instance;
    }
    bool Initialize(void* device = nullptr) { return true; }
    void Shutdown() {}
    void Generate(const TerrainConfig& config) { m_config = config; }
    void GenerateFoliage(const FoliageConfig& config) { m_foliageConfig = config; }
    float GetHeightAt(float worldX, float worldZ) const { return 0.0f; }
    XMFLOAT3 GetNormalAt(float worldX, float worldZ) const { return XMFLOAT3(0.0f, 1.0f, 0.0f); }
    bool IsOnTerrain(float worldX, float worldZ) const { return false; }
    void Update(float dt, const XMFLOAT3& cameraPos) {}
    void Render(void* commandList,
                const XMMATRIX& viewProj,
                const XMFLOAT3& lightDir,
                const XMFLOAT3& lightColor,
                const XMFLOAT3& cameraPos,
                float time) {}
    void ApplyPresetRollingHills() {}
    void ApplyPresetAlpineMountains() {}
    void ApplyPresetPlains() {}
    void DrawUI(const XMFLOAT3& cameraPos) {}
#endif

    bool IsUIOpen() const { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI() { m_uiOpen = !m_uiOpen; }

    const TerrainConfig& GetConfig() const { return m_config; }
    const FoliageConfig& GetFoliageConfig() const { return m_foliageConfig; }

private:
#if defined(_WIN32)
    TerrainSystem();
    ~TerrainSystem();
#else
    TerrainSystem() = default;
    ~TerrainSystem() = default;
#endif
    TerrainSystem(const TerrainSystem&) = delete;
    TerrainSystem& operator=(const TerrainSystem&) = delete;

#if defined(_WIN32)
    float SampleFBM(float x, float z, int octaves, float persistence, float lacunarity) const;
    float PerlinNoise2D(float x, float z) const;

    bool CreatePipelines();
    bool CreateBuffers();
    void UploadTerrainGeometry();
    void UploadFoliageGeometry();
#endif

    TerrainConfig m_config;
    FoliageConfig m_foliageConfig;

    std::vector<float> m_heightmap;
    std::vector<TerrainVertex> m_vertices;
    std::vector<uint32_t> m_indices;

    std::vector<FoliageVertex> m_foliageVertices;
    std::vector<uint32_t> m_foliageIndices;
    std::vector<FoliageInstance> m_foliageInstances;

    bool m_initialized = false;
    bool m_uiOpen = false;
    float m_windTime = 0.0f;

#if defined(_WIN32)
    // DX12 Resources
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_terrainPSO;
    ComPtr<ID3D12PipelineState> m_foliagePSO;

    ComPtr<ID3D12Resource> m_terrainVB;
    ComPtr<ID3D12Resource> m_terrainIB;
    ComPtr<ID3D12Resource> m_foliageVB;
    ComPtr<ID3D12Resource> m_foliageIB;
    ComPtr<ID3D12Resource> m_constantBuffer;

    D3D12_VERTEX_BUFFER_VIEW m_terrainVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_terrainIBView = {};
    D3D12_VERTEX_BUFFER_VIEW m_foliageVBView = {};
    D3D12_INDEX_BUFFER_VIEW  m_foliageIBView = {};

    TerrainConstantBuffer* m_mappedCbv = nullptr;
#endif

    // Permutation table for Perlin noise
    int m_perm[512] = {};
#if defined(_WIN32)
    void InitNoise(int seed);
#endif
};

} // namespace EngineTerrain
