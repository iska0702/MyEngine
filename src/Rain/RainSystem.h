#pragma once
#include "RainTypes.h"
#include "RainParticle.h"
#include "WeatherSystem.h"
#include "Renderer/Camera.h"
#include <vector>
#include <memory>

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#endif

class Renderer;

namespace EngineRain {

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
#endif

class RainSystem {
public:
    static RainSystem& Get();

#if defined(_WIN32)
    bool Initialize(ID3D12Device* device);
    void Render(ID3D12GraphicsCommandList* commandList, const XMMATRIX& viewProj, const XMFLOAT3& lightColor, float lightningIntensity, const XMFLOAT3& cameraPos, float time, const XMFLOAT3& lightDir);
#else
    bool Initialize(void* device = nullptr);
    void Render(void* commandList, const XMMATRIX& viewProj, const XMFLOAT3& lightColor, float lightningIntensity, const XMFLOAT3& cameraPos, float time, const XMFLOAT3& lightDir);
#endif
    void Shutdown();

    void Update(float dt, const XMFLOAT3& cameraPos, const std::vector<Camera::CollisionBox>& collisionBoxes);

    RainSettings& GetSettings() { return m_settings; }
    const RainSettings& GetSettings() const { return m_settings; }

    WeatherSystem& GetWeatherSystem() { return m_weather; }
    const WeatherSystem& GetWeatherSystem() const { return m_weather; }

    const RainDebugStats& GetDebugStats() const { return m_debugStats; }

    void SetWeatherPreset(WeatherPreset preset) { m_weather.SetPreset(preset, m_settings); }

private:
    RainSystem();
    ~RainSystem();

    RainSettings m_settings;
    WeatherSystem m_weather;
    RainDebugStats m_debugStats;

    std::vector<RainDrop> m_particles;
    std::vector<RainSplash> m_splashes;

    std::vector<RainVertex> m_streakVertices;
    std::vector<uint32_t> m_streakIndices;

    std::vector<RainVertex> m_splashVertices;
    std::vector<uint32_t> m_splashIndices;

    float m_accumulatedTime = 0.0f;
    int m_recentCollisions = 0;
    float m_collisionCounterTimer = 0.0f;
    size_t m_nextSplashIndex = 0;

#if defined(_WIN32)
    // D3D12 Resources
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_streakPipelineState;
    ComPtr<ID3D12PipelineState> m_splashPipelineState;

    ComPtr<ID3D12Resource> m_streakVertexBuffer;
    ComPtr<ID3D12Resource> m_streakIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_streakVBView = {};
    D3D12_INDEX_BUFFER_VIEW m_streakIBView = {};

    ComPtr<ID3D12Resource> m_splashVertexBuffer;
    ComPtr<ID3D12Resource> m_splashIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_splashVBView = {};
    D3D12_INDEX_BUFFER_VIEW m_splashIBView = {};

    struct RainCB {
        XMMATRIX viewProj;
        XMFLOAT4 lightColorAndIntensity; // rgb = light color, a = lightning flash
        XMFLOAT4 cameraPosAndTime;       // xyz = cameraPos, w = time
        XMFLOAT4 lightDir;               // xyz = lightDir, w = padding
    };
    ComPtr<ID3D12Resource> m_constantBuffer;
    RainCB* m_pCbvData = nullptr;
#endif

    size_t m_maxStreaks = 0;
    size_t m_maxSplashes = 0;

    bool CreatePipelines();
    bool CreateBuffers(size_t maxStreaks, size_t maxSplashes);
    void SpawnSplash(const XMFLOAT3& pos, const XMFLOAT3& normal);
    void ReallocateParticles(size_t count);
};

} // namespace EngineRain
