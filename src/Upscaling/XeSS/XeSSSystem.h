#pragma once

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <vector>
#include <functional>

// Intel XeSS SDK Headers
#include "xess/xess.h"
#include "xess/xess_d3d12.h"
#include "xess/xess_debug.h"
#include "xess/xess_d3d12_debug.h"
#include "xess/xess_d3d11.h"
#include "xess_fg/xefg_swapchain.h"
#include "xess_fg/xefg_swapchain_d3d12.h"
#include "xefg_swapchain_debug.h"
#include "xell/xell.h"
#include "xell/xell_d3d12.h"

#include "Renderer/Common/RenderTypes.h"

namespace EngineRender {

/**
 * @brief Comprehensive wrapper and manager for Intel XeSS 2.0 SDK suite:
 * - XeSS Super Resolution (SR) for D3D12 / D3D11
 * - XeSS Frame Generation (XeFG) with DXGI proxy swapchain
 * - Intel XeLL (Xe Low Latency) pipeline optimization and latency reduction
 * - XeSS Debug, Profiling, and Dump inspection API
 */
class XeSSSystem {
public:
    XeSSSystem();
    ~XeSSSystem();

    // Non-copyable
    XeSSSystem(const XeSSSystem&) = delete;
    XeSSSystem& operator=(const XeSSSystem&) = delete;

    // =========================================================================
    // High-Level Super Resolution (SR) Lifecycle & Execution
    // =========================================================================
    bool Initialize(ID3D12Device* device,
                    uint32_t outputWidth,
                    uint32_t outputHeight,
                    xess_quality_settings_t quality = XESS_QUALITY_SETTING_QUALITY,
                    uint32_t initFlags = XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE);

    void Shutdown();

    bool Resize(uint32_t outputWidth, uint32_t outputHeight, xess_quality_settings_t quality);

    bool ExecuteSR(ID3D12GraphicsCommandList* commandList,
                   ID3D12Resource* colorTexture,
                   ID3D12Resource* velocityTexture,
                   ID3D12Resource* depthTexture,
                   ID3D12Resource* outputTexture,
                   float jitterX,
                   float jitterY,
                   bool resetHistory = false,
                   float exposureScale = 1.0f);

    bool IsInitialized() const { return m_initialized; }
    bool IsSupported() const { return m_supported; }
    xess_context_handle_t GetContext() const { return m_xessContext; }
    xess_2d_t GetInputResolution() const { return m_inputResolution; }
    xess_2d_t GetOutputResolution() const { return m_outputResolution; }
    xess_quality_settings_t GetQuality() const { return m_quality; }

    // =========================================================================
    // High-Level Frame Generation (XeFG) Lifecycle & Management
    // =========================================================================
    bool InitializeFrameGeneration(ID3D12Device* device,
                                   ID3D12CommandQueue* commandQueue,
                                   IDXGISwapChain* appSwapChain,
                                   uint32_t maxInterpolatedFrames = 1,
                                   uint32_t initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE);

    void ShutdownFrameGeneration();
    bool InitializeFrameGenerationForWindow(ID3D12Device* device, ID3D12CommandQueue* queue,
        HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGIFactory2* factory);

    bool SetFrameGenEnabled(bool enabled);
    bool IsFrameGenEnabled() const { return m_frameGenEnabled; }
    bool IsFrameGenSupported() const { return m_frameGenSupported; }
    xefg_swapchain_handle_t GetFGContext() const { return m_xefgContext; }
    IDXGISwapChain* GetFGSwapChain() const { return m_proxySwapChain.Get(); }

    bool TagFrameConstantsFG(uint32_t presentId, const xefg_swapchain_frame_constant_data_t& constants);
    bool TagFrameResourceFG(ID3D12CommandList* cmdList, uint32_t presentId, const xefg_swapchain_d3d12_resource_data_t& resData);
    xefg_swapchain_present_status_t GetLastPresentStatusFG();

    // =========================================================================
    // High-Level Intel XeLL (Low Latency) Lifecycle & Management
    // =========================================================================
    bool InitializeLowLatency(ID3D12Device* device, bool lowLatencyMode = true, bool boost = false, uint32_t minIntervalUs = 0);
    void ShutdownLowLatency();
    bool SetLowLatencyMode(bool enabled, bool boost = false, uint32_t minIntervalUs = 0);
    bool SleepLowLatency(uint32_t frameId);
    bool AddLatencyMarker(uint32_t frameId, xell_latency_marker_type_t marker);
    bool IsLowLatencySupported() const { return m_xellSupported; }
    bool IsLowLatencyEnabled() const { return m_xellEnabled; }
    xell_context_handle_t GetXeLLContext() const { return m_xellContext; }

    // =========================================================================
    // Core Intel XeSS API Functions (Direct SDK Wrappers)
    // =========================================================================
    static xess_result_t GetVersion(xess_version_t* pVersion);
    xess_result_t GetIntelXeFXVersion(xess_version_t* pVersion) const;
    xess_result_t GetProperties(const xess_2d_t* pOutputResolution, xess_properties_t* pBindingProperties) const;
    xess_result_t GetInputResolution(const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings, xess_2d_t* pInputResolution) const;
    xess_result_t GetOptimalInputResolution(const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings,
                                           xess_2d_t* pOptimal, xess_2d_t* pMin, xess_2d_t* pMax) const;
    xess_result_t GetJitterScale(float* pX, float* pY) const;
    xess_result_t GetVelocityScale(float* pX, float* pY) const;
    xess_result_t DestroyContext();
    xess_result_t SetJitterScale(float x, float y);
    xess_result_t SetVelocityScale(float x, float y);
    xess_result_t SetExposureMultiplier(float scale);
    xess_result_t GetExposureMultiplier(float* pScale) const;
    xess_result_t SetMaxResponsiveMaskValue(float value);
    xess_result_t GetMaxResponsiveMaskValue(float* pValue) const;
    xess_result_t SetLoggingCallback(xess_logging_level_t loggingLevel, xess_app_log_callback_t loggingCallback);
    xess_result_t IsOptimalDriver() const;
    xess_result_t ForceLegacyScaleFactors(bool force);
    xess_result_t GetPipelineBuildStatus() const;

    // =========================================================================
    // XeSS D3D12 Specific API Functions
    // =========================================================================
    xess_result_t D3D12CreateContext(ID3D12Device* pDevice);
    xess_result_t D3D12BuildPipelines(ID3D12PipelineLibrary* pPipelineLibrary, bool blocking, uint32_t initFlags);
    xess_result_t D3D12Init(const xess_d3d12_init_params_t* pInitParams);
    xess_result_t D3D12GetInitParams(xess_d3d12_init_params_t* pInitParams) const;
    xess_result_t D3D12Execute(ID3D12GraphicsCommandList* pCommandList, const xess_d3d12_execute_params_t* pExecParams);

    // =========================================================================
    // XeSS Debug & Profiling Functions
    // =========================================================================
    xess_result_t SelectNetworkModel(xess_network_model_t network);
    xess_result_t StartDump(const xess_dump_parameters_t* dump_parameters);
    xess_result_t GetProfilingData(xess_profiling_data_t** pProfilingData) const;
    xess_result_t D3D12GetResourcesToDump(xess_resources_to_dump_t** pResourcesToDump) const;
    xess_result_t D3D12GetProfilingData(xess_profiling_data_t** pProfilingData) const;

    // =========================================================================
    // XeSS D3D11 API Functions
    // =========================================================================
    static xess_result_t D3D11CreateContext(ID3D11Device* device, xess_context_handle_t* phContext);
    static xess_result_t D3D11Init(xess_context_handle_t hContext, const xess_d3d11_init_params_t* pInitParams);
    static xess_result_t D3D11GetInitParams(xess_context_handle_t hContext, xess_d3d11_init_params_t* pInitParams);
    static xess_result_t D3D11Execute(xess_context_handle_t hContext, const xess_d3d11_execute_params_t* pExecParams);

    // =========================================================================
    // XeSS Frame Generation (XeFG) Specific API Functions
    // =========================================================================
    static xefg_swapchain_result_t FGGetVersion(xefg_swapchain_version_t* pVersion);
    xefg_swapchain_result_t FGGetProperties(xefg_swapchain_properties_t* pProperties) const;
    xefg_swapchain_result_t FGTagFrameConstants(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* pConstants);
    xefg_swapchain_result_t FGSetEnabled(uint32_t enable);
    xefg_swapchain_result_t FGSetPresentId(uint32_t presentId);
    xefg_swapchain_result_t FGGetLastPresentStatus(xefg_swapchain_present_status_t* pPresentStatus) const;
    xefg_swapchain_result_t FGSetLoggingCallback(xefg_swapchain_logging_level_t loggingLevel, xefg_swapchain_app_log_callback_t loggingCallback, void* userData);
    xefg_swapchain_result_t FGDestroy();
    xefg_swapchain_result_t FGSetLatencyReduction(void* hXeLLContext);
    xefg_swapchain_result_t FGSetSceneChangeThreshold(float threshold);
    xefg_swapchain_result_t FGGetPipelineBuildStatus() const;
    xefg_swapchain_result_t FGSetNumInterpolatedFrames(uint32_t numInterpolatedFrames);
    xefg_swapchain_result_t FGSetUiCompositionState(xefg_swapchain_ui_composition_state_t state);
    xefg_swapchain_result_t FGEnableDebugFeature(xefg_swapchain_debug_feature_t featureId, uint32_t enable, void* pArgument);
    xefg_swapchain_result_t FGD3D12CreateContext(ID3D12Device* pDevice);
    xefg_swapchain_result_t FGD3D12BuildPipelines(ID3D12PipelineLibrary* pPipelineLibrary, uint8_t blocking, uint32_t initFlags);
    xefg_swapchain_result_t FGD3D12GetProperties(const xefg_swapchain_d3d12_init_params_t* initParams,
                                                uint32_t backBufferWidth, uint32_t backBufferHeight,
                                                DXGI_FORMAT backBufferFormat, xefg_swapchain_properties_t* properties) const;
    xefg_swapchain_result_t FGD3D12InitFromSwapChain(ID3D12CommandQueue* pCmdQueue, const xefg_swapchain_d3d12_init_params_t* pInitParams);
    xefg_swapchain_result_t FGD3D12InitFromSwapChainDesc(HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pSwapChainDesc,
                                                        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                        ID3D12CommandQueue* pCmdQueue, IDXGIFactory2* pDxgiFactory,
                                                        const xefg_swapchain_d3d12_init_params_t* pInitParams);
    xefg_swapchain_result_t FGD3D12GetSwapChainPtr(REFIID riid, void** ppSwapChain);
    xefg_swapchain_result_t FGD3D12TagFrameResource(ID3D12CommandList* pCmdList, uint32_t presentId, const xefg_swapchain_d3d12_resource_data_t* pResData);
    xefg_swapchain_result_t FGD3D12SetDescriptorHeap(ID3D12DescriptorHeap* pDescriptorHeap, uint32_t descriptorHeapOffsetInBytes);
    xefg_swapchain_result_t FGD3D12UpdateExternalHeapOnResize(ID3D12Heap* tempBufferHeap, uint64_t tempBufferHeapOffset,
                                                            ID3D12Heap* tempTextureHeap, uint64_t tempTextureHeapOffset);
    xefg_swapchain_result_t FGD3D12GetInitializationParameters(xefg_swapchain_d3d12_init_params_t* pParams) const;

    // =========================================================================
    // Intel XeLL (Low Latency) Specific API Functions
    // =========================================================================
    xell_result_t XeLLD3D12CreateContext(ID3D12Device* device);
    xell_result_t XeLLDestroyContext();
    xell_result_t XeLLSetSleepMode(const xell_sleep_params_t* param);
    xell_result_t XeLLGetSleepMode(xell_sleep_params_t* param) const;
    xell_result_t XeLLSleep(uint32_t frame_id);
    xell_result_t XeLLAddMarkerData(uint32_t frame_id, xell_latency_marker_type_t marker);
    static xell_result_t XeLLGetVersion(xell_version_t* pVersion);
    xell_result_t XeLLSetLoggingCallback(xell_logging_level_t loggingLevel, xell_app_log_callback_t loggingCallback);
    xell_result_t XeLLGetFramesReports(xell_frame_report_t* outdata) const;

    // =========================================================================
    // Utilities & Conversions
    // =========================================================================
    static xess_quality_settings_t EngineQualityToXeSS(UpscaleQuality quality);
    static xess_quality_settings_t EngineQualityToXeSS(int quality);
    template <typename T>
    static xess_quality_settings_t EngineQualityToXeSS(T quality) {
        return EngineQualityToXeSS(static_cast<int>(quality));
    }
    static UpscaleQuality XeSSToEngineQuality(xess_quality_settings_t quality);
    static const char* ResultToString(xess_result_t result);
    static const char* FGResultToString(xefg_swapchain_result_t result);
    static const char* XeLLResultToString(xell_result_t result);

private:
    // Core XeSS State
    xess_context_handle_t m_xessContext = nullptr;
    bool m_supported = false;
    bool m_initialized = false;
    xess_2d_t m_outputResolution = { 0, 0 };
    xess_2d_t m_inputResolution = { 0, 0 };
    xess_quality_settings_t m_quality = XESS_QUALITY_SETTING_QUALITY;
    uint32_t m_initFlags = 0;

    // XeSS-FG State
    xefg_swapchain_handle_t m_xefgContext = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_proxySwapChain;
    bool m_frameGenSupported = false;
    bool m_frameGenEnabled = false;

    // XeLL State
    xell_context_handle_t m_xellContext = nullptr;
    bool m_xellSupported = false;
    bool m_xellEnabled = false;
};

} // namespace EngineRender
#endif // _WIN32
