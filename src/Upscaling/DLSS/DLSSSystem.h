#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <DirectXMath.h>

// Streamline & DLSS SDK Headers
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_matrix_helpers.h>

#include "Renderer/Common/RenderTypes.h"

namespace EngineRender {

using Microsoft::WRL::ComPtr;

/**
 * @brief Comprehensive manager and wrapper for NVIDIA Streamline and DLSS technologies:
 * - DLSS Super Resolution (DLSS-SR 3.x / 4.x)
 * - DLSS Frame Generation (DLSS-G)
 * - DLSS Ray Reconstruction (DLSS-RR / DLSS-D)
 * - NVIDIA Reflex Low Latency
 */
class DLSSSystem {
public:
    DLSSSystem();
    ~DLSSSystem();

    DLSSSystem(const DLSSSystem&) = delete;
    DLSSSystem& operator=(const DLSSSystem&) = delete;

    // =========================================================================
    // Lifecycle
    // =========================================================================
    bool PrecheckAndInitializeStreamline();
    bool SetD3DDevice(ID3D12Device* device);
    void Shutdown();

    bool IsStreamlineInitialized() const { return m_streamlineInitialized; }
    bool IsDLSSSupported() const { return m_dlssSupported; }
    bool IsDLSSActive() const { return m_dlssActive; }
    bool IsDLSSGSupported() const { return m_dlssGSupported; }
    bool IsDLSSGActive() const { return m_dlssGActive; }
    bool IsDLSSGRequested() const { return m_dlssGRequested; }
    int  GetDLSSGMaxMultiplier() const { return m_dlssGMaxMultiplier; }
    bool IsDLSSRRSupported() const { return m_dlssRRSupported; }
    bool IsDLSSRRActive() const { return m_dlssRRActive; }
    bool IsReflexSupported() const { return m_reflexSupported; }

    void SetActive(bool active) { m_dlssActive = active; }
    void SetNeedsReset(bool reset) { m_dlssNeedsReset = reset; }
    bool NeedsReset() const { return m_dlssNeedsReset; }

    // =========================================================================
    // Optimal Resolution Calculation
    // =========================================================================
    bool GetOptimalSettings(uint32_t targetWidth, uint32_t targetHeight,
                            UpscaleQuality quality,
                            uint32_t& outRenderWidth, uint32_t& outRenderHeight,
                            float& outSharpness);

    // =========================================================================
    // Reflex Controls
    // =========================================================================
    bool SetReflexMode(ReflexMode mode);
    bool SleepReflex(const sl::FrameToken& frameToken);

    // =========================================================================
    // DLSS-G Frame Generation
    // =========================================================================
    void SetDLSSGRequested(bool requested) { m_dlssGRequested = requested; }
    bool ConfigureDLSSG(bool enable, uint32_t multiplier, ID3D12CommandQueue* queue);
    bool TagFrameGenResources(const sl::FrameToken& frameToken,
                              ID3D12GraphicsCommandList* cmdList,
                              ID3D12Resource* hudlessColor,
                              ID3D12Resource* uiColor,
                              ID3D12Resource* depth,
                              ID3D12Resource* mvec,
                              const sl::Constants& constants);
    void PollFrameGenState();
    uint64_t GetGeneratedFrames() const { return m_dlssGeneratedFrames; }
    ID3D12Fence* GetFrameGenFence() const { return m_dlssGFence.Get(); }
    uint64_t GetFrameGenFenceValue() const { return m_dlssGFenceValue; }

    // =========================================================================
    // Presentation Upgrade
    // =========================================================================
    bool UpgradeSwapChain(IDXGISwapChain* swapChain);

    // =========================================================================
    // Frame Evaluation (DLSS-SR / DLSS-RR)
    // =========================================================================
    struct EvaluateParams {
        const sl::FrameToken* frameToken = nullptr;
        ID3D12GraphicsCommandList* commandList = nullptr;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t targetWidth = 0;
        uint32_t targetHeight = 0;
        float sharpness = 0.0f;
        UpscaleQuality quality = UpscaleQuality::Quality;
        bool reset = false;

        ID3D12Resource* inputColor = nullptr;
        ID3D12Resource* outputColor = nullptr;
        ID3D12Resource* depth = nullptr;
        ID3D12Resource* velocity = nullptr;

        // Camera parameters for constants
        DirectX::XMFLOAT3 cameraPos = {0,0,0};
        DirectX::XMFLOAT3 cameraUp = {0,1,0};
        DirectX::XMFLOAT3 cameraRight = {1,0,0};
        DirectX::XMFLOAT3 cameraFwd = {0,0,1};
        DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX projMatrix = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX cleanViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMMATRIX prevCleanViewProj = DirectX::XMMatrixIdentity();
        DirectX::XMFLOAT2 jitter = {0,0};
        float fov = 60.0f;
        float nearZ = 0.1f;
        float farZ = 2000.0f;

        // Ray Reconstruction inputs (optional)
        bool rrRequested = false;
        DLSSRRVersion rrVersion = DLSSRRVersion::Off;
        ID3D12Resource* rrDiffuseAlbedo = nullptr;
        ID3D12Resource* rrSpecularAlbedo = nullptr;
        ID3D12Resource* normalTexture = nullptr;
        ID3D12Resource* rrHitDistance = nullptr;
    };

    bool Evaluate(const EvaluateParams& params);

private:
    bool m_streamlineInitialized = false;
    bool m_dlssSupported = false;
    bool m_dlssActive = false;
    bool m_dlssNeedsReset = true;

    bool m_dlssGSupported = false;
    bool m_dlssGActive = false;
    bool m_dlssGRequested = false;
    bool m_dlssGConfigured = false;
    int  m_dlssGMaxMultiplier = 2;
    uint64_t m_dlssGeneratedFrames = 0;
    uint64_t m_dlssGFenceValue = 0;
    ComPtr<ID3D12Fence> m_dlssGFence;

    bool m_dlssRRSupported = false;
    bool m_dlssRRActive = false;

    bool m_reflexSupported = false;
    ReflexMode m_reflexMode = ReflexMode::Off;
    bool m_reflexOptionsApplied = false;

    std::wstring m_nvidiaDir;
};

} // namespace EngineRender
#endif // _WIN32
