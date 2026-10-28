#pragma once

#include "ParticleEmitter.h"
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

class Renderer;

namespace EngineVFX {

#if defined(_WIN32)
using Microsoft::WRL::ComPtr;
#endif

class ParticleSystem {
public:
#if defined(_WIN32)
    static ParticleSystem& Get();
    bool Initialize(ID3D12Device* device);
    void Shutdown();
    void Update(float dt, const XMFLOAT3& cameraPos, const XMFLOAT3& cameraUp);
    void Render(ID3D12GraphicsCommandList* commandList,
                const XMMATRIX& viewProj,
                const XMFLOAT3& cameraPos,
                float time);
    size_t GetTotalActiveParticles() const;

    // High-Level Preset Spawners
    std::shared_ptr<ParticleEmitter> SpawnEmitter(const ParticleEmitterConfig& config);
    void SpawnExplosion(const XMFLOAT3& position, float scale = 1.0f);
    void SpawnFire(const XMFLOAT3& position, float intensity = 1.0f);
    void SpawnSmoke(const XMFLOAT3& position, float duration = 6.0f);
    void SpawnSparks(const XMFLOAT3& position, const XMFLOAT3& direction, size_t count = 40);
    void SpawnMagicEnergy(const XMFLOAT3& position, const XMFLOAT4& color = { 0.3f, 0.7f, 1.0f, 1.0f });

    void ClearEmitters();

    // Editor UI
    void DrawUI(const XMFLOAT3& cameraPos, const XMFLOAT3& cameraForward);
#else
    static ParticleSystem& Get() {
        static ParticleSystem instance;
        return instance;
    }
    bool Initialize(void* device = nullptr) { return true; }
    void Shutdown() {}
    void Update(float, const XMFLOAT3&, const XMFLOAT3&) {}
    void Render(void*, const XMMATRIX&, const XMFLOAT3&, float) {}
    size_t GetTotalActiveParticles() const { return 0; }

    std::shared_ptr<ParticleEmitter> SpawnEmitter(const ParticleEmitterConfig&) { return nullptr; }
    void SpawnExplosion(const XMFLOAT3&, float = 1.0f) {}
    void SpawnFire(const XMFLOAT3&, float = 1.0f) {}
    void SpawnSmoke(const XMFLOAT3&, float = 6.0f) {}
    void SpawnSparks(const XMFLOAT3&, const XMFLOAT3&, size_t = 40) {}
    void SpawnMagicEnergy(const XMFLOAT3&, const XMFLOAT4& = { 0.3f, 0.7f, 1.0f, 1.0f }) {}

    void ClearEmitters() {}
    void DrawUI(const XMFLOAT3&, const XMFLOAT3&) {}
#endif

    bool IsUIOpen() const { return m_uiOpen; }
    void SetUIOpen(bool open) { m_uiOpen = open; }
    void ToggleUI() { m_uiOpen = !m_uiOpen; }

    size_t GetActiveEmitterCount() const { return m_emitters.size(); }

private:
#if defined(_WIN32)
    ParticleSystem();
    ~ParticleSystem();
    bool CreatePipelines();
    bool CreateBuffers();
#else
    ParticleSystem() = default;
    ~ParticleSystem() = default;
#endif

    std::vector<std::shared_ptr<ParticleEmitter>> m_emitters;
    bool m_initialized = false;
    bool m_uiOpen = false;

#if defined(_WIN32)
    // DX12 Resources
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_additivePSO;
    ComPtr<ID3D12PipelineState> m_alphaPSO;

    // Dynamic GPU Buffers for drawing
    static constexpr size_t MAX_RENDER_VERTICES = 65536;
    static constexpr size_t MAX_RENDER_INDICES  = 98304;

    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_constantBuffer;

    D3D12_VERTEX_BUFFER_VIEW m_vbView = {};
    D3D12_INDEX_BUFFER_VIEW  m_ibView = {};
    ParticleConstantBuffer*  m_mappedCbv = nullptr;
    ParticleVertex*          m_mappedVb  = nullptr;
    uint32_t*                m_mappedIb  = nullptr;
#endif
};

} // namespace EngineVFX
