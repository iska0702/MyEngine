#include "XeSSSystem.h"

#if defined(_WIN32)
#include <iostream>
#include <sstream>

#define XESS_LOG(msg) std::cout << "[XeSS] " << msg << std::endl
#define XESS_ERR(msg) std::cerr << "[XeSS ERROR] " << msg << std::endl
#define XESS_WARN(msg) std::cout << "[XeSS WARNING] " << msg << std::endl

namespace EngineRender {

static void IntelXeSSLogCallback(const char* message, xess_logging_level_t loggingLevel) {
    if (!message) return;
    switch (loggingLevel) {
        case XESS_LOGGING_LEVEL_DEBUG:
            std::cout << "[XeSS-SDK Debug] " << message << std::endl;
            break;
        case XESS_LOGGING_LEVEL_INFO:
            std::cout << "[XeSS-SDK Info] " << message << std::endl;
            break;
        case XESS_LOGGING_LEVEL_WARNING:
            std::cout << "[XeSS-SDK Warning] " << message << std::endl;
            break;
        case XESS_LOGGING_LEVEL_ERROR:
            std::cerr << "[XeSS-SDK Error] " << message << std::endl;
            break;
    }
}

static void IntelXeFGLogCallback(const char* message, xefg_swapchain_logging_level_t loggingLevel, void* userData) {
    (void)userData;
    if (!message) return;
    std::cout << "[XeFG-SDK] " << message << std::endl;
}

static void IntelXeLLLogCallback(const char* message, xell_logging_level_t loggingLevel) {
    if (!message) return;
    std::cout << "[XeLL-SDK] " << message << std::endl;
}

XeSSSystem::XeSSSystem() {
}

XeSSSystem::~XeSSSystem() {
    Shutdown();
    ShutdownFrameGeneration();
    ShutdownLowLatency();
}

// =============================================================================
// High-Level Super Resolution (SR) Lifecycle & Execution
// =============================================================================

bool XeSSSystem::Initialize(ID3D12Device* device,
                            uint32_t outputWidth,
                            uint32_t outputHeight,
                            xess_quality_settings_t quality,
                            uint32_t initFlags) {
    if (!device || outputWidth == 0 || outputHeight == 0) {
        XESS_ERR("Initialize failed: invalid device or resolution (" << outputWidth << "x" << outputHeight << ")");
        return false;
    }

    if (m_initialized) {
        Shutdown();
    }

    m_outputResolution = { outputWidth, outputHeight };
    m_quality = quality;
    m_initFlags = initFlags;

    // 1. Query SDK version
    xess_version_t sdkVersion = {};
    if (GetVersion(&sdkVersion) == XESS_RESULT_SUCCESS) {
        XESS_LOG("Loaded Intel XeSS SDK version: " << sdkVersion.major << "." << sdkVersion.minor << "." << sdkVersion.patch);
    }

    // 2. Create context
    xess_result_t res = D3D12CreateContext(device);
    if (res != XESS_RESULT_SUCCESS) {
        XESS_ERR("Failed to create XeSS D3D12 context: " << ResultToString(res));
        m_supported = false;
        return false;
    }

    m_supported = true;

    // Optional logging callback
    SetLoggingCallback(XESS_LOGGING_LEVEL_WARNING, IntelXeSSLogCallback);

    // 3. Driver optimal check
    xess_result_t driverStatus = IsOptimalDriver();
    if (driverStatus == XESS_RESULT_WARNING_OLD_DRIVER) {
        XESS_WARN("Installed graphics driver may be outdated for optimal XeSS performance.");
    }

    // 4. Query XeFX version
    xess_version_t xefxVersion = {};
    if (GetIntelXeFXVersion(&xefxVersion) == XESS_RESULT_SUCCESS && xefxVersion.major > 0) {
        XESS_LOG("Intel XeFX library version: " << xefxVersion.major << "." << xefxVersion.minor << "." << xefxVersion.patch);
    }

    // 5. Query internal properties & heap requirements
    xess_properties_t props = {};
    if (GetProperties(&m_outputResolution, &props) == XESS_RESULT_SUCCESS) {
        XESS_LOG("XeSS Properties: required descriptors=" << props.requiredDescriptorCount
                 << ", tempBuffer=" << (props.tempBufferHeapSize / 1024) << " KB"
                 << ", tempTexture=" << (props.tempTextureHeapSize / (1024 * 1024)) << " MB");
    }

    // 6. Calculate optimal input resolution
    xess_2d_t optRes = {}, minRes = {}, maxRes = {};
    res = GetOptimalInputResolution(&m_outputResolution, m_quality, &optRes, &minRes, &maxRes);
    if (res == XESS_RESULT_SUCCESS) {
        m_inputResolution = optRes;
        XESS_LOG("Optimal input resolution for " << outputWidth << "x" << outputHeight
                 << " @ quality " << (int)m_quality << " is " << optRes.x << "x" << optRes.y
                 << " (range: " << minRes.x << "x" << minRes.y << " to " << maxRes.x << "x" << maxRes.y << ")");
    } else {
        // Fallback to basic input resolution query
        res = GetInputResolution(&m_outputResolution, m_quality, &m_inputResolution);
        if (res != XESS_RESULT_SUCCESS) {
            XESS_WARN("GetInputResolution failed (" << ResultToString(res) << "), using default 1.5x scale");
            m_inputResolution = { (outputWidth * 2) / 3, (outputHeight * 2) / 3 };
        }
    }

    // 7. Initialize XeSS D3D12 context (internal heap management)
    xess_d3d12_init_params_t initParams = {};
    initParams.outputResolution = m_outputResolution;
    initParams.qualitySetting = m_quality;
    initParams.initFlags = m_initFlags;
    initParams.creationNodeMask = 0;
    initParams.visibleNodeMask = 0;
    initParams.pTempBufferHeap = nullptr;
    initParams.bufferHeapOffset = 0;
    initParams.pTempTextureHeap = nullptr;
    initParams.textureHeapOffset = 0;
    initParams.pPipelineLibrary = nullptr;

    res = D3D12Init(&initParams);
    if (res != XESS_RESULT_SUCCESS) {
        XESS_ERR("xessD3D12Init failed: " << ResultToString(res));
        DestroyContext();
        return false;
    }

    m_initialized = true;
    XESS_LOG("Intel XeSS initialized successfully (" << m_inputResolution.x << "x" << m_inputResolution.y
             << " -> " << m_outputResolution.x << "x" << m_outputResolution.y << ")");
    return true;
}

void XeSSSystem::Shutdown() {
    ShutdownFrameGeneration();
    ShutdownLowLatency();
    try {
        if (m_xessContext) {
            DestroyContext();
            m_xessContext = nullptr;
        }
    } catch (...) {}
    m_initialized = false;
    m_supported = false;
}

bool XeSSSystem::Resize(uint32_t outputWidth, uint32_t outputHeight, xess_quality_settings_t quality) {
    if (!m_initialized && !m_xessContext) return false;
    try {
        m_outputResolution = { outputWidth, outputHeight };
        m_quality = quality;

        // Query new input resolution
        xess_2d_t optRes = {}, minRes = {}, maxRes = {};
        if (GetOptimalInputResolution(&m_outputResolution, m_quality, &optRes, &minRes, &maxRes) == XESS_RESULT_SUCCESS) {
            m_inputResolution = optRes;
        } else {
            GetInputResolution(&m_outputResolution, m_quality, &m_inputResolution);
        }

        // Re-initialize with new parameters
        xess_d3d12_init_params_t initParams = {};
        initParams.outputResolution = m_outputResolution;
        initParams.qualitySetting = m_quality;
        initParams.initFlags = m_initFlags;
        initParams.creationNodeMask = 0;
        initParams.visibleNodeMask = 0;
        initParams.pTempBufferHeap = nullptr;
        initParams.bufferHeapOffset = 0;
        initParams.pTempTextureHeap = nullptr;
        initParams.textureHeapOffset = 0;
        initParams.pPipelineLibrary = nullptr;

        xess_result_t res = D3D12Init(&initParams);
        if (res != XESS_RESULT_SUCCESS) {
            XESS_ERR("Resize failed during xessD3D12Init: " << ResultToString(res));
            m_initialized = false;
            return false;
        }

        m_initialized = true;
        XESS_LOG("XeSS Resized: " << m_inputResolution.x << "x" << m_inputResolution.y << " -> " << m_outputResolution.x << "x" << m_outputResolution.y);
        return true;
    } catch (...) {
        XESS_ERR("XeSS caught exception during Resize");
        m_initialized = false;
        return false;
    }
}

bool XeSSSystem::ExecuteSR(ID3D12GraphicsCommandList* commandList,
                           ID3D12Resource* colorTexture,
                           ID3D12Resource* velocityTexture,
                           ID3D12Resource* depthTexture,
                           ID3D12Resource* outputTexture,
                           float jitterX,
                           float jitterY,
                           bool resetHistory,
                           float exposureScale) {
    if (!m_initialized || !m_xessContext || !commandList || !colorTexture || !velocityTexture || !outputTexture) {
        return false;
    }

    xess_d3d12_execute_params_t execParams = {};
    execParams.pColorTexture = colorTexture;
    execParams.pVelocityTexture = velocityTexture;
    execParams.pDepthTexture = depthTexture;
    execParams.pExposureScaleTexture = nullptr;
    execParams.pResponsivePixelMaskTexture = nullptr;
    execParams.pOutputTexture = outputTexture;

    execParams.jitterOffsetX = jitterX;
    execParams.jitterOffsetY = jitterY;
    execParams.exposureScale = exposureScale;
    execParams.resetHistory = resetHistory ? 1 : 0;
    execParams.inputWidth = m_inputResolution.x;
    execParams.inputHeight = m_inputResolution.y;
    execParams.inputColorBase = { 0, 0 };
    execParams.inputMotionVectorBase = { 0, 0 };
    execParams.inputDepthBase = { 0, 0 };
    execParams.inputResponsiveMaskBase = { 0, 0 };
    execParams.outputColorBase = { 0, 0 };
    execParams.pDescriptorHeap = nullptr;
    execParams.descriptorHeapOffset = 0;

    try {
        xess_result_t res = D3D12Execute(commandList, &execParams);
        if (res != XESS_RESULT_SUCCESS) {
            static int errCount = 0;
            if (errCount++ % 100 == 0) {
                XESS_ERR("xessD3D12Execute failed: " << ResultToString(res));
            }
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// =============================================================================
// High-Level Frame Generation (XeFG) Lifecycle & Management
// =============================================================================

bool XeSSSystem::InitializeFrameGeneration(ID3D12Device* device,
                                          ID3D12CommandQueue* commandQueue,
                                          IDXGISwapChain* appSwapChain,
                                          uint32_t maxInterpolatedFrames,
                                          uint32_t initFlags) {
    if (!device || !commandQueue) return false;

    try {
        xefg_swapchain_version_t fgVer = {};
        if (FGGetVersion(&fgVer) == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            XESS_LOG("Loaded Intel XeSS Frame Generation SDK version: " << fgVer.major << "." << fgVer.minor << "." << fgVer.patch);
        }

        xefg_swapchain_result_t res = FGD3D12CreateContext(device);
        if (res != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            XESS_WARN("XeSS-FG D3D12 CreateContext not supported or failed: " << FGResultToString(res));
            m_frameGenSupported = false;
            m_frameGenEnabled = false;
            return false;
        }

        m_frameGenSupported = true;
        FGSetLoggingCallback(XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING, IntelXeFGLogCallback, nullptr);

        if (m_xellContext) {
            FGSetLatencyReduction(m_xellContext);
        }

        if (appSwapChain) {
            xefg_swapchain_d3d12_init_params_t initParams = {};
            initParams.pApplicationSwapChain = appSwapChain;
            initParams.initFlags = initFlags;
            initParams.maxInterpolatedFrames = maxInterpolatedFrames;
            initParams.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;

            res = FGD3D12InitFromSwapChain(commandQueue, &initParams);
            if (res == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
                IDXGISwapChain* proxyChain = nullptr;
                if (FGD3D12GetSwapChainPtr(IID_PPV_ARGS(&proxyChain)) == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
                    m_proxySwapChain = proxyChain;
                    if (proxyChain) proxyChain->Release();
                }
                m_frameGenEnabled = true;
                XESS_LOG("Intel XeSS Frame Generation initialized successfully!");
                return true;
            } else {
                XESS_WARN("XeSS-FG InitFromSwapChain failed: " << FGResultToString(res));
                m_frameGenEnabled = false;
                m_frameGenSupported = false;
                if (m_xefgContext) {
                    FGDestroy();
                    m_xefgContext = nullptr;
                }
                m_proxySwapChain.Reset();
                return false;
            }
        }
    } catch (...) {
        XESS_WARN("XeSS-FG caught unexpected exception during initialization");
        m_frameGenEnabled = false;
        m_frameGenSupported = false;
        if (m_xefgContext) {
            FGDestroy();
            m_xefgContext = nullptr;
        }
        m_proxySwapChain.Reset();
        return false;
    }

    return true;
}

void XeSSSystem::ShutdownFrameGeneration() {
    m_proxySwapChain.Reset();
    if (m_xefgContext) {
        FGDestroy();
        m_xefgContext = nullptr;
    }
    m_proxySwapChain.Reset();
    m_frameGenEnabled = false;
    m_frameGenSupported = false;
}

bool XeSSSystem::InitializeFrameGenerationForWindow(ID3D12Device* device,ID3D12CommandQueue* queue,
    HWND hwnd,const DXGI_SWAP_CHAIN_DESC1& desc,IDXGIFactory2* factory) {
    ShutdownFrameGeneration();
    auto result=FGD3D12CreateContext(device);
    if(result!=XEFG_SWAPCHAIN_RESULT_SUCCESS){XESS_WARN("XeFG context: "<<FGResultToString(result));return false;}
    FGSetLoggingCallback(XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING,IntelXeFGLogCallback,nullptr);
    if(m_xellContext)FGSetLatencyReduction(m_xellContext);
    xefg_swapchain_d3d12_init_params_t params{};params.maxInterpolatedFrames=1;
    params.uiMode=XEFG_SWAPCHAIN_UI_MODE_BACKBUFFER_HUDLESS;
    result=FGD3D12InitFromSwapChainDesc(hwnd,&desc,nullptr,queue,factory,&params);
    if(result==XEFG_SWAPCHAIN_RESULT_SUCCESS) {
        IDXGISwapChain* proxy=nullptr;
        result=FGD3D12GetSwapChainPtr(IID_PPV_ARGS(&proxy));
        if(result==XEFG_SWAPCHAIN_RESULT_SUCCESS&&proxy){m_proxySwapChain.Attach(proxy);m_frameGenSupported=true;return SetFrameGenEnabled(true);}
    }
    XESS_WARN("XeFG swapchain: "<<FGResultToString(result));ShutdownFrameGeneration();return false;
}

bool XeSSSystem::SetFrameGenEnabled(bool enabled) {
    if (!m_xefgContext) return false;
    xefg_swapchain_result_t res = FGSetEnabled(enabled ? 1 : 0);
    if (res == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
        m_frameGenEnabled = enabled;
        return true;
    }
    return false;
}

bool XeSSSystem::TagFrameConstantsFG(uint32_t presentId, const xefg_swapchain_frame_constant_data_t& constants) {
    if (!m_xefgContext) return false;
    return FGTagFrameConstants(presentId, &constants) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

bool XeSSSystem::TagFrameResourceFG(ID3D12CommandList* cmdList, uint32_t presentId, const xefg_swapchain_d3d12_resource_data_t& resData) {
    if (!m_xefgContext) return false;
    return FGD3D12TagFrameResource(cmdList, presentId, &resData) == XEFG_SWAPCHAIN_RESULT_SUCCESS;
}

xefg_swapchain_present_status_t XeSSSystem::GetLastPresentStatusFG() {
    xefg_swapchain_present_status_t status = {};
    if (m_xefgContext) {
        FGGetLastPresentStatus(&status);
    }
    return status;
}

// =============================================================================
// High-Level Intel XeLL (Low Latency) Lifecycle & Management
// =============================================================================

bool XeSSSystem::InitializeLowLatency(ID3D12Device* device, bool lowLatencyMode, bool boost, uint32_t minIntervalUs) {
    if (!device) return false;

    try {
        xell_version_t xellVer = {};
        if (XeLLGetVersion(&xellVer) == XELL_RESULT_SUCCESS) {
            XESS_LOG("Loaded Intel XeLL (Low Latency) version: " << xellVer.major << "." << xellVer.minor << "." << xellVer.patch);
        }

        xell_result_t res = XeLLD3D12CreateContext(device);
        if (res != XELL_RESULT_SUCCESS) {
            XESS_WARN("Intel XeLL CreateContext failed: " << XeLLResultToString(res));
            m_xellSupported = false;
            m_xellEnabled = false;
            return false;
        }

        m_xellSupported = true;
        XeLLSetLoggingCallback(XELL_LOGGING_LEVEL_WARNING, IntelXeLLLogCallback);

        xell_sleep_params_t sleepParams = {};
        sleepParams.bLowLatencyMode = lowLatencyMode ? 1 : 0;
        sleepParams.bLowLatencyBoost = boost ? 1 : 0;
        sleepParams.minimumIntervalUs = minIntervalUs;
        XeLLSetSleepMode(&sleepParams);

        m_xellEnabled = lowLatencyMode;
        XESS_LOG("Intel XeLL Low Latency initialized (enabled=" << lowLatencyMode << ", boost=" << boost << ")");
        return true;
    } catch (...) {
        XESS_WARN("Intel XeLL caught exception during initialization (non-Intel GPU or unsupported driver)");
        m_xellSupported = false;
        m_xellEnabled = false;
        if (m_xellContext) {
            XeLLDestroyContext();
            m_xellContext = nullptr;
        }
        return false;
    }
}

void XeSSSystem::ShutdownLowLatency() {
    try {
        if (m_xellContext) {
            XeLLDestroyContext();
            m_xellContext = nullptr;
        }
    } catch (...) {}
    m_xellSupported = false;
    m_xellEnabled = false;
}

bool XeSSSystem::SetLowLatencyMode(bool enabled, bool boost, uint32_t minIntervalUs) {
    if (!m_xellContext) return false;
    try {
        xell_sleep_params_t sleepParams = {};
        sleepParams.bLowLatencyMode = enabled ? 1 : 0;
        sleepParams.bLowLatencyBoost = boost ? 1 : 0;
        sleepParams.minimumIntervalUs = minIntervalUs;
        xell_result_t res = XeLLSetSleepMode(&sleepParams);
        if (res == XELL_RESULT_SUCCESS) {
            m_xellEnabled = enabled;
            return true;
        }
    } catch (...) {}
    return false;
}

bool XeSSSystem::SleepLowLatency(uint32_t frameId) {
    if (!m_xellContext || !m_xellEnabled) return false;
    try {
        return XeLLSleep(frameId) == XELL_RESULT_SUCCESS;
    } catch (...) {
        return false;
    }
}

bool XeSSSystem::AddLatencyMarker(uint32_t frameId, xell_latency_marker_type_t marker) {
    if (!m_xellContext || !m_xellEnabled) return false;
    try {
        return XeLLAddMarkerData(frameId, marker) == XELL_RESULT_SUCCESS;
    } catch (...) {
        return false;
    }
}

// =============================================================================
// Core Intel XeSS API Functions (Direct SDK Wrappers)
// =============================================================================

xess_result_t XeSSSystem::GetVersion(xess_version_t* pVersion) {
    return xessGetVersion(pVersion);
}

xess_result_t XeSSSystem::GetIntelXeFXVersion(xess_version_t* pVersion) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetIntelXeFXVersion(m_xessContext, pVersion);
}

xess_result_t XeSSSystem::GetProperties(const xess_2d_t* pOutputResolution, xess_properties_t* pBindingProperties) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetProperties(m_xessContext, pOutputResolution, pBindingProperties);
}

xess_result_t XeSSSystem::GetInputResolution(const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings, xess_2d_t* pInputResolution) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetInputResolution(m_xessContext, pOutputResolution, qualitySettings, pInputResolution);
}

xess_result_t XeSSSystem::GetOptimalInputResolution(const xess_2d_t* pOutputResolution, xess_quality_settings_t qualitySettings,
                                                    xess_2d_t* pOptimal, xess_2d_t* pMin, xess_2d_t* pMax) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetOptimalInputResolution(m_xessContext, pOutputResolution, qualitySettings, pOptimal, pMin, pMax);
}

xess_result_t XeSSSystem::GetJitterScale(float* pX, float* pY) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetJitterScale(m_xessContext, pX, pY);
}

xess_result_t XeSSSystem::GetVelocityScale(float* pX, float* pY) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetVelocityScale(m_xessContext, pX, pY);
}

xess_result_t XeSSSystem::DestroyContext() {
    if (!m_xessContext) return XESS_RESULT_SUCCESS;
    xess_result_t res = xessDestroyContext(m_xessContext);
    m_xessContext = nullptr;
    return res;
}

xess_result_t XeSSSystem::SetJitterScale(float x, float y) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSetJitterScale(m_xessContext, x, y);
}

xess_result_t XeSSSystem::SetVelocityScale(float x, float y) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSetVelocityScale(m_xessContext, x, y);
}

xess_result_t XeSSSystem::SetExposureMultiplier(float scale) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSetExposureMultiplier(m_xessContext, scale);
}

xess_result_t XeSSSystem::GetExposureMultiplier(float* pScale) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetExposureMultiplier(m_xessContext, pScale);
}

xess_result_t XeSSSystem::SetMaxResponsiveMaskValue(float value) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSetMaxResponsiveMaskValue(m_xessContext, value);
}

xess_result_t XeSSSystem::GetMaxResponsiveMaskValue(float* pValue) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetMaxResponsiveMaskValue(m_xessContext, pValue);
}

xess_result_t XeSSSystem::SetLoggingCallback(xess_logging_level_t loggingLevel, xess_app_log_callback_t loggingCallback) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSetLoggingCallback(m_xessContext, loggingLevel, loggingCallback);
}

xess_result_t XeSSSystem::IsOptimalDriver() const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessIsOptimalDriver(m_xessContext);
}

xess_result_t XeSSSystem::ForceLegacyScaleFactors(bool force) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessForceLegacyScaleFactors(m_xessContext, force);
}

xess_result_t XeSSSystem::GetPipelineBuildStatus() const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetPipelineBuildStatus(m_xessContext);
}

// =============================================================================
// XeSS D3D12 Specific API Functions
// =============================================================================

xess_result_t XeSSSystem::D3D12CreateContext(ID3D12Device* pDevice) {
    if (!pDevice) return XESS_RESULT_ERROR_INVALID_ARGUMENT;
    return xessD3D12CreateContext(pDevice, &m_xessContext);
}

xess_result_t XeSSSystem::D3D12BuildPipelines(ID3D12PipelineLibrary* pPipelineLibrary, bool blocking, uint32_t initFlags) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12BuildPipelines(m_xessContext, pPipelineLibrary, blocking, initFlags);
}

xess_result_t XeSSSystem::D3D12Init(const xess_d3d12_init_params_t* pInitParams) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12Init(m_xessContext, pInitParams);
}

xess_result_t XeSSSystem::D3D12GetInitParams(xess_d3d12_init_params_t* pInitParams) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12GetInitParams(m_xessContext, pInitParams);
}

xess_result_t XeSSSystem::D3D12Execute(ID3D12GraphicsCommandList* pCommandList, const xess_d3d12_execute_params_t* pExecParams) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12Execute(m_xessContext, pCommandList, pExecParams);
}

// =============================================================================
// XeSS Debug & Profiling Functions
// =============================================================================

xess_result_t XeSSSystem::SelectNetworkModel(xess_network_model_t network) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessSelectNetworkModel(m_xessContext, network);
}

xess_result_t XeSSSystem::StartDump(const xess_dump_parameters_t* dump_parameters) {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessStartDump(m_xessContext, dump_parameters);
}

xess_result_t XeSSSystem::GetProfilingData(xess_profiling_data_t** pProfilingData) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessGetProfilingData(m_xessContext, pProfilingData);
}

xess_result_t XeSSSystem::D3D12GetResourcesToDump(xess_resources_to_dump_t** pResourcesToDump) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12GetResourcesToDump(m_xessContext, pResourcesToDump);
}

xess_result_t XeSSSystem::D3D12GetProfilingData(xess_profiling_data_t** pProfilingData) const {
    if (!m_xessContext) return XESS_RESULT_ERROR_UNINITIALIZED;
    return xessD3D12GetProfilingData(m_xessContext, pProfilingData);
}

// =============================================================================
// XeSS D3D11 API Functions
// =============================================================================

xess_result_t XeSSSystem::D3D11CreateContext(ID3D11Device* device, xess_context_handle_t* phContext) {
    return xessD3D11CreateContext(device, phContext);
}

xess_result_t XeSSSystem::D3D11Init(xess_context_handle_t hContext, const xess_d3d11_init_params_t* pInitParams) {
    return xessD3D11Init(hContext, pInitParams);
}

xess_result_t XeSSSystem::D3D11GetInitParams(xess_context_handle_t hContext, xess_d3d11_init_params_t* pInitParams) {
    return xessD3D11GetInitParams(hContext, pInitParams);
}

xess_result_t XeSSSystem::D3D11Execute(xess_context_handle_t hContext, const xess_d3d11_execute_params_t* pExecParams) {
    return xessD3D11Execute(hContext, pExecParams);
}

// =============================================================================
// XeSS Frame Generation (XeFG) Specific API Functions
// =============================================================================

xefg_swapchain_result_t XeSSSystem::FGGetVersion(xefg_swapchain_version_t* pVersion) {
    return xefgSwapChainGetVersion(pVersion);
}

xefg_swapchain_result_t XeSSSystem::FGGetProperties(xefg_swapchain_properties_t* pProperties) const {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainGetProperties(m_xefgContext, pProperties);
}

xefg_swapchain_result_t XeSSSystem::FGTagFrameConstants(uint32_t presentId, const xefg_swapchain_frame_constant_data_t* pConstants) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainTagFrameConstants(m_xefgContext, presentId, pConstants);
}

xefg_swapchain_result_t XeSSSystem::FGSetEnabled(uint32_t enable) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetEnabled(m_xefgContext, enable);
}

xefg_swapchain_result_t XeSSSystem::FGSetPresentId(uint32_t presentId) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetPresentId(m_xefgContext, presentId);
}

xefg_swapchain_result_t XeSSSystem::FGGetLastPresentStatus(xefg_swapchain_present_status_t* pPresentStatus) const {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainGetLastPresentStatus(m_xefgContext, pPresentStatus);
}

xefg_swapchain_result_t XeSSSystem::FGSetLoggingCallback(xefg_swapchain_logging_level_t loggingLevel, xefg_swapchain_app_log_callback_t loggingCallback, void* userData) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetLoggingCallback(m_xefgContext, loggingLevel, loggingCallback, userData);
}

xefg_swapchain_result_t XeSSSystem::FGDestroy() {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_SUCCESS;
    xefg_swapchain_result_t res = xefgSwapChainDestroy(m_xefgContext);
    m_xefgContext = nullptr;
    return res;
}

xefg_swapchain_result_t XeSSSystem::FGSetLatencyReduction(void* hXeLLContext) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetLatencyReduction(m_xefgContext, hXeLLContext);
}

xefg_swapchain_result_t XeSSSystem::FGSetSceneChangeThreshold(float threshold) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetSceneChangeThreshold(m_xefgContext, threshold);
}

xefg_swapchain_result_t XeSSSystem::FGGetPipelineBuildStatus() const {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainGetPipelineBuildStatus(m_xefgContext);
}

xefg_swapchain_result_t XeSSSystem::FGSetNumInterpolatedFrames(uint32_t numInterpolatedFrames) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetNumInterpolatedFrames(m_xefgContext, numInterpolatedFrames);
}

xefg_swapchain_result_t XeSSSystem::FGSetUiCompositionState(xefg_swapchain_ui_composition_state_t state) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainSetUiCompositionState(m_xefgContext, state);
}

xefg_swapchain_result_t XeSSSystem::FGEnableDebugFeature(xefg_swapchain_debug_feature_t featureId, uint32_t enable, void* pArgument) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainEnableDebugFeature(m_xefgContext, featureId, enable, pArgument);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12CreateContext(ID3D12Device* pDevice) {
    if (!pDevice) return XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT;
    return xefgSwapChainD3D12CreateContext(pDevice, &m_xefgContext);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12BuildPipelines(ID3D12PipelineLibrary* pPipelineLibrary, uint8_t blocking, uint32_t initFlags) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12BuildPipelines(m_xefgContext, pPipelineLibrary, blocking, initFlags);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12GetProperties(const xefg_swapchain_d3d12_init_params_t* initParams,
                                                        uint32_t backBufferWidth, uint32_t backBufferHeight,
                                                        DXGI_FORMAT backBufferFormat, xefg_swapchain_properties_t* properties) const {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12GetProperties(m_xefgContext, initParams, backBufferWidth, backBufferHeight, backBufferFormat, properties);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12InitFromSwapChain(ID3D12CommandQueue* pCmdQueue, const xefg_swapchain_d3d12_init_params_t* pInitParams) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12InitFromSwapChain(m_xefgContext, pCmdQueue, pInitParams);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12InitFromSwapChainDesc(HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pSwapChainDesc,
                                                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                                ID3D12CommandQueue* pCmdQueue, IDXGIFactory2* pDxgiFactory,
                                                                const xefg_swapchain_d3d12_init_params_t* pInitParams) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12InitFromSwapChainDesc(m_xefgContext, hWnd, pSwapChainDesc, pFullscreenDesc, pCmdQueue, pDxgiFactory, pInitParams);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12GetSwapChainPtr(REFIID riid, void** ppSwapChain) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12GetSwapChainPtr(m_xefgContext, riid, ppSwapChain);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12TagFrameResource(ID3D12CommandList* pCmdList, uint32_t presentId, const xefg_swapchain_d3d12_resource_data_t* pResData) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12TagFrameResource(m_xefgContext, pCmdList, presentId, pResData);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12SetDescriptorHeap(ID3D12DescriptorHeap* pDescriptorHeap, uint32_t descriptorHeapOffsetInBytes) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12SetDescriptorHeap(m_xefgContext, pDescriptorHeap, descriptorHeapOffsetInBytes);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12UpdateExternalHeapOnResize(ID3D12Heap* tempBufferHeap, uint64_t tempBufferHeapOffset,
                                                                    ID3D12Heap* tempTextureHeap, uint64_t tempTextureHeapOffset) {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12UpdateExternalHeapOnResize(m_xefgContext, tempBufferHeap, tempBufferHeapOffset, tempTextureHeap, tempTextureHeapOffset);
}

xefg_swapchain_result_t XeSSSystem::FGD3D12GetInitializationParameters(xefg_swapchain_d3d12_init_params_t* pParams) const {
    if (!m_xefgContext) return XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED;
    return xefgSwapChainD3D12GetInitializationParameters(m_xefgContext, pParams);
}

// =============================================================================
// Intel XeLL (Low Latency) Specific API Functions
// =============================================================================

xell_result_t XeSSSystem::XeLLD3D12CreateContext(ID3D12Device* device) {
    if (!device) return XELL_RESULT_ERROR_INVALID_ARGUMENT;
    return xellD3D12CreateContext(device, &m_xellContext);
}

xell_result_t XeSSSystem::XeLLDestroyContext() {
    if (!m_xellContext) return XELL_RESULT_SUCCESS;
    xell_result_t res = xellDestroyContext(m_xellContext);
    m_xellContext = nullptr;
    return res;
}

xell_result_t XeSSSystem::XeLLSetSleepMode(const xell_sleep_params_t* param) {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellSetSleepMode(m_xellContext, param);
}

xell_result_t XeSSSystem::XeLLGetSleepMode(xell_sleep_params_t* param) const {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellGetSleepMode(m_xellContext, param);
}

xell_result_t XeSSSystem::XeLLSleep(uint32_t frame_id) {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellSleep(m_xellContext, frame_id);
}

xell_result_t XeSSSystem::XeLLAddMarkerData(uint32_t frame_id, xell_latency_marker_type_t marker) {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellAddMarkerData(m_xellContext, frame_id, marker);
}

xell_result_t XeSSSystem::XeLLGetVersion(xell_version_t* pVersion) {
    return xellGetVersion(pVersion);
}

xell_result_t XeSSSystem::XeLLSetLoggingCallback(xell_logging_level_t loggingLevel, xell_app_log_callback_t loggingCallback) {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellSetLoggingCallback(m_xellContext, loggingLevel, loggingCallback);
}

xell_result_t XeSSSystem::XeLLGetFramesReports(xell_frame_report_t* outdata) const {
    if (!m_xellContext) return XELL_RESULT_ERROR_UNINITIALIZED;
    return xellGetFramesReports(m_xellContext, outdata);
}

// =============================================================================
// Utilities & Conversions
// =============================================================================

xess_quality_settings_t XeSSSystem::EngineQualityToXeSS(UpscaleQuality quality) {
    return EngineQualityToXeSS(static_cast<int>(quality));
}

xess_quality_settings_t XeSSSystem::EngineQualityToXeSS(int quality) {
    switch (quality) {
        case 0: // Native / AA
            return XESS_QUALITY_SETTING_AA;
        case 1: // Quality
            return XESS_QUALITY_SETTING_QUALITY;
        case 2: // Balanced
            return XESS_QUALITY_SETTING_BALANCED;
        case 3: // Performance
            return XESS_QUALITY_SETTING_PERFORMANCE;
        case 4: // UltraPerformance
            return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
        default:
            return XESS_QUALITY_SETTING_QUALITY;
    }
}

UpscaleQuality XeSSSystem::XeSSToEngineQuality(xess_quality_settings_t quality) {
    switch (quality) {
        case XESS_QUALITY_SETTING_AA:
            return UpscaleQuality::Native;
        case XESS_QUALITY_SETTING_QUALITY:
        case XESS_QUALITY_SETTING_ULTRA_QUALITY:
        case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS:
            return UpscaleQuality::Quality;
        case XESS_QUALITY_SETTING_BALANCED:
            return UpscaleQuality::Balanced;
        case XESS_QUALITY_SETTING_PERFORMANCE:
            return UpscaleQuality::Performance;
        case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE:
            return UpscaleQuality::UltraPerformance;
        default:
            return UpscaleQuality::Quality;
    }
}

const char* XeSSSystem::ResultToString(xess_result_t result) {
    switch (result) {
        case XESS_RESULT_SUCCESS: return "XESS_RESULT_SUCCESS";
        case XESS_RESULT_WARNING_NONEXISTING_FOLDER: return "XESS_RESULT_WARNING_NONEXISTING_FOLDER";
        case XESS_RESULT_WARNING_OLD_DRIVER: return "XESS_RESULT_WARNING_OLD_DRIVER";
        case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return "XESS_RESULT_ERROR_UNSUPPORTED_DEVICE";
        case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return "XESS_RESULT_ERROR_UNSUPPORTED_DRIVER";
        case XESS_RESULT_ERROR_UNINITIALIZED: return "XESS_RESULT_ERROR_UNINITIALIZED";
        case XESS_RESULT_ERROR_INVALID_ARGUMENT: return "XESS_RESULT_ERROR_INVALID_ARGUMENT";
        case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY";
        case XESS_RESULT_ERROR_DEVICE: return "XESS_RESULT_ERROR_DEVICE";
        case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return "XESS_RESULT_ERROR_NOT_IMPLEMENTED";
        case XESS_RESULT_ERROR_INVALID_CONTEXT: return "XESS_RESULT_ERROR_INVALID_CONTEXT";
        case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "XESS_RESULT_ERROR_OPERATION_IN_PROGRESS";
        case XESS_RESULT_ERROR_UNSUPPORTED: return "XESS_RESULT_ERROR_UNSUPPORTED";
        case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return "XESS_RESULT_ERROR_CANT_LOAD_LIBRARY";
        case XESS_RESULT_ERROR_WRONG_CALL_ORDER: return "XESS_RESULT_ERROR_WRONG_CALL_ORDER";
        case XESS_RESULT_ERROR_UNKNOWN: return "XESS_RESULT_ERROR_UNKNOWN";
        default: return "XESS_RESULT_UNKNOWN_CODE";
    }
}

const char* XeSSSystem::FGResultToString(xefg_swapchain_result_t result) {
    switch (result) {
        case XEFG_SWAPCHAIN_RESULT_SUCCESS: return "XEFG_SWAPCHAIN_RESULT_SUCCESS";
        case XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER: return "XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER";
        case XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES: return "XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES";
        case XEFG_SWAPCHAIN_RESULT_WARNING_FRAMES_ID_MISMATCH: return "XEFG_SWAPCHAIN_RESULT_WARNING_FRAMES_ID_MISMATCH";
        case XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS: return "XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS";
        case XEFG_SWAPCHAIN_RESULT_WARNING_RESOURCE_SIZES_MISMATCH: return "XEFG_SWAPCHAIN_RESULT_WARNING_RESOURCE_SIZES_MISMATCH";
        case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE: return "XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DEVICE";
        case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DRIVER: return "XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED_DRIVER";
        case XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED: return "XEFG_SWAPCHAIN_RESULT_ERROR_UNINITIALIZED";
        case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT: return "XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_ARGUMENT";
        case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE_OUT_OF_MEMORY";
        case XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE: return "XEFG_SWAPCHAIN_RESULT_ERROR_DEVICE";
        case XEFG_SWAPCHAIN_RESULT_ERROR_NOT_IMPLEMENTED: return "XEFG_SWAPCHAIN_RESULT_ERROR_NOT_IMPLEMENTED";
        case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT: return "XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_CONTEXT";
        case XEFG_SWAPCHAIN_RESULT_ERROR_OPERATION_IN_PROGRESS: return "XEFG_SWAPCHAIN_RESULT_ERROR_OPERATION_IN_PROGRESS";
        case XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED: return "XEFG_SWAPCHAIN_RESULT_ERROR_UNSUPPORTED";
        case XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY: return "XEFG_SWAPCHAIN_RESULT_ERROR_CANT_LOAD_LIBRARY";
        case XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES: return "XEFG_SWAPCHAIN_RESULT_ERROR_MISMATCH_INPUT_RESOURCES";
        case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_OUTPUT_RESOURCES: return "XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_OUTPUT_RESOURCES";
        case XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES: return "XEFG_SWAPCHAIN_RESULT_ERROR_INCORRECT_INPUT_RESOURCES";
        case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_UNSUPPORTED: return "XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_UNSUPPORTED";
        case XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_FUNCTION_MISSING: return "XEFG_SWAPCHAIN_RESULT_ERROR_LATENCY_REDUCTION_FUNCTION_MISSING";
        case XEFG_SWAPCHAIN_RESULT_ERROR_HRESULT_FAILURE: return "XEFG_SWAPCHAIN_RESULT_ERROR_HRESULT_FAILURE";
        case XEFG_SWAPCHAIN_RESULT_ERROR_DXGI_INVALID_CALL: return "XEFG_SWAPCHAIN_RESULT_ERROR_DXGI_INVALID_CALL";
        case XEFG_SWAPCHAIN_RESULT_ERROR_POINTER_STILL_IN_USE: return "XEFG_SWAPCHAIN_RESULT_ERROR_POINTER_STILL_IN_USE";
        case XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_DESCRIPTOR_HEAP: return "XEFG_SWAPCHAIN_RESULT_ERROR_INVALID_DESCRIPTOR_HEAP";
        case XEFG_SWAPCHAIN_RESULT_ERROR_WRONG_CALL_ORDER: return "XEFG_SWAPCHAIN_RESULT_ERROR_WRONG_CALL_ORDER";
        case XEFG_SWAPCHAIN_RESULT_ERROR_UNKNOWN: return "XEFG_SWAPCHAIN_RESULT_ERROR_UNKNOWN";
        default: return "XEFG_SWAPCHAIN_RESULT_UNKNOWN_CODE";
    }
}

const char* XeSSSystem::XeLLResultToString(xell_result_t result) {
    switch (result) {
        case XELL_RESULT_SUCCESS: return "XELL_RESULT_SUCCESS";
        case XELL_RESULT_ERROR_UNSUPPORTED_DEVICE: return "XELL_RESULT_ERROR_UNSUPPORTED_DEVICE";
        case XELL_RESULT_ERROR_UNSUPPORTED_DRIVER: return "XELL_RESULT_ERROR_UNSUPPORTED_DRIVER";
        case XELL_RESULT_ERROR_UNINITIALIZED: return "XELL_RESULT_ERROR_UNINITIALIZED";
        case XELL_RESULT_ERROR_INVALID_ARGUMENT: return "XELL_RESULT_ERROR_INVALID_ARGUMENT";
        case XELL_RESULT_ERROR_DEVICE: return "XELL_RESULT_ERROR_DEVICE";
        case XELL_RESULT_ERROR_NOT_IMPLEMENTED: return "XELL_RESULT_ERROR_NOT_IMPLEMENTED";
        case XELL_RESULT_ERROR_INVALID_CONTEXT: return "XELL_RESULT_ERROR_INVALID_CONTEXT";
        case XELL_RESULT_ERROR_UNSUPPORTED: return "XELL_RESULT_ERROR_UNSUPPORTED";
        case XELL_RESULT_ERROR_UNKNOWN: return "XELL_RESULT_ERROR_UNKNOWN";
        default: return "XELL_RESULT_UNKNOWN_CODE";
    }
}

} // namespace EngineRender
#endif // _WIN32
