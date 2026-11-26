#if defined(_WIN32)
#include "DLSSSystem.h"
#include "Core/EngineLog.h"
#include <algorithm>
#include <sstream>

namespace EngineRender {

DLSSSystem::DLSSSystem() = default;

DLSSSystem::~DLSSSystem() {
    Shutdown();
}

bool DLSSSystem::PrecheckAndInitializeStreamline() {
    wchar_t exePath[MAX_PATH];
    std::wstring exeDir;
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::wstring ws(exePath);
        size_t lastSlash = ws.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeDir = ws.substr(0, lastSlash);
            SetCurrentDirectoryW(exeDir.c_str());
        }
    }

    m_nvidiaDir = exeDir + L"\\ThirdParty\\NVIDIA\\Streamline";
    if (GetFileAttributesW(m_nvidiaDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        m_nvidiaDir = exeDir + L"\\ThirdParty\\NVIDIA";
    }
    SetDllDirectoryW(m_nvidiaDir.c_str());

    auto tryLoad = [&](const wchar_t* dllName) {
        auto toUtf8 = [](const wchar_t* ws) -> std::string {
            if (!ws) return {};
            int required = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
            if (required <= 0) return {};
            std::string out(required - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), required, nullptr, nullptr);
            return out;
        };

        std::wstring fullPath = m_nvidiaDir + L"\\" + dllName;
        HMODULE h = LoadLibraryW(fullPath.c_str());
        if (h) {
            FreeLibrary(h);
            EngineLog::Write("Precheck LoadLibrary OK: " + toUtf8(fullPath.c_str()));
        } else {
            DWORD err = GetLastError();
            EngineLog::Write("Precheck LoadLibrary FAILED: " + toUtf8(fullPath.c_str()) + " GetLastError=" + std::to_string(err));
        }
    };

    tryLoad(L"nvngx_dlss.dll");
    tryLoad(L"nvngx_dlssd.dll");
    tryLoad(L"nvngx_deepdvc.dll");
    tryLoad(L"nvngx_dlssg.dll");

    sl::Preferences slPref = {};
    slPref.showConsole = false;
    slPref.logLevel = sl::LogLevel::eDefault;
    slPref.logMessageCallback = [](sl::LogType, const char* message) {
        if (message) EngineLog::Write(std::string("[Streamline] ") + message);
    };
    slPref.applicationId = 1;

    sl::Feature featuresToLoad[] = {
        sl::kFeatureDLSS,
        sl::kFeatureReflex,
        sl::kFeatureDLSS_G,
        sl::kFeaturePCL,
        sl::kFeatureDLSS_RR
    };
    slPref.featuresToLoad = featuresToLoad;
    slPref.numFeaturesToLoad = uint32_t(std::size(featuresToLoad));
    slPref.renderAPI = sl::RenderAPI::eD3D12;
    slPref.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging | sl::PreferenceFlags::eDisableDebugText;

    static std::wstring s_absPluginNvidia;
    static std::wstring s_absExeDir;
    s_absExeDir = exeDir;
    s_absPluginNvidia = m_nvidiaDir;
    const wchar_t* pluginPaths[] = { s_absPluginNvidia.c_str(), s_absExeDir.c_str() };
    slPref.pathsToPlugins = pluginPaths;
    slPref.numPathsToPlugins = 2;

    sl::Result slInitRes = slInit(slPref);
    m_streamlineInitialized = (slInitRes == sl::Result::eOk);
    if (!m_streamlineInitialized) {
        EngineLog::Write("Streamline slInit FAILED (code=" + std::to_string((int)slInitRes) + ")");
    }
    return m_streamlineInitialized;
}

bool DLSSSystem::SetD3DDevice(ID3D12Device* device) {
    if (!device || !m_streamlineInitialized) return false;

    if (slSetD3DDevice(device) != sl::Result::eOk) {
        EngineLog::Write("Streamline slSetD3DDevice FAILED");
        return false;
    }
    EngineLog::Write("Streamline D3D Device Set");

    LUID adapterLuid = device->GetAdapterLuid();
    sl::AdapterInfo adapterInfo = {};
    adapterInfo.deviceLUID = (uint8_t*)&adapterLuid;
    adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

    if (slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk) {
        m_dlssSupported = true;
        EngineLog::Write("DLSS Hardware: Supported");
    } else {
        m_dlssSupported = false;
        EngineLog::Write("DLSS Hardware: Not Supported, falling back to Native/FSR2");
    }

    if (slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk) {
        m_dlssGSupported = true;
        sl::DLSSGState capabilities{};
        if (slDLSSGGetState(sl::ViewportHandle{0}, capabilities, nullptr) == sl::Result::eOk) {
            m_dlssGMaxMultiplier = (std::max)(2, int((std::min)(capabilities.numFramesToGenerateMax, 31u)) + 1);
        }
        EngineLog::Write("DLSS-G Frame Generation Hardware: Supported");
    } else {
        m_dlssGSupported = false;
        EngineLog::Write("DLSS-G Frame Generation Hardware: Not Supported");
    }

    if (slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo) == sl::Result::eOk) {
        m_dlssRRSupported = true;
        EngineLog::Write("DLSS-RR Ray Reconstruction Hardware: Supported");
    } else {
        m_dlssRRSupported = false;
        EngineLog::Write("DLSS-RR Ray Reconstruction Hardware: Not Supported");
    }

    if (slIsFeatureSupported(sl::kFeatureReflex, adapterInfo) == sl::Result::eOk) {
        m_reflexSupported = true;
        EngineLog::Write("Reflex Hardware: Supported");
        SetReflexMode(m_reflexMode);
    } else {
        m_reflexSupported = false;
        EngineLog::Write("Reflex Hardware: Not Supported");
    }

    return true;
}

void DLSSSystem::Shutdown() {
    if (m_streamlineInitialized) {
        slShutdown();
        m_streamlineInitialized = false;
        m_dlssSupported = false;
        m_dlssGSupported = false;
        m_dlssRRSupported = false;
        m_reflexSupported = false;
    }
}

bool DLSSSystem::GetOptimalSettings(uint32_t targetWidth, uint32_t targetHeight,
                                    UpscaleQuality quality,
                                    uint32_t& outRenderWidth, uint32_t& outRenderHeight,
                                    float& outSharpness) {
    if (!m_dlssSupported) return false;

    sl::DLSSOptions dlssOpts = {};
    switch (quality) {
        case UpscaleQuality::Quality: dlssOpts.mode = sl::DLSSMode::eMaxQuality; break;
        case UpscaleQuality::Balanced: dlssOpts.mode = sl::DLSSMode::eBalanced; break;
        case UpscaleQuality::Performance: dlssOpts.mode = sl::DLSSMode::eMaxPerformance; break;
        case UpscaleQuality::UltraPerformance: dlssOpts.mode = sl::DLSSMode::eUltraPerformance; break;
        case UpscaleQuality::Native: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
        default: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
    }
    dlssOpts.outputWidth = targetWidth;
    dlssOpts.outputHeight = targetHeight;

    sl::DLSSOptimalSettings dlssSettings = {};
    if (slDLSSGetOptimalSettings(dlssOpts, dlssSettings) == sl::Result::eOk) {
        outRenderWidth = dlssSettings.optimalRenderWidth;
        outRenderHeight = dlssSettings.optimalRenderHeight;
        outSharpness = dlssSettings.optimalSharpness;
        m_dlssNeedsReset = true;
        return true;
    }
    return false;
}

bool DLSSSystem::SetReflexMode(ReflexMode mode) {
    m_reflexMode = mode;
    if (!m_reflexSupported) return false;

    sl::ReflexOptions options{};
    options.mode = static_cast<sl::ReflexMode>(int(mode));
    m_reflexOptionsApplied = (slReflexSetOptions(options) == sl::Result::eOk);
    return m_reflexOptionsApplied;
}

bool DLSSSystem::SleepReflex(const sl::FrameToken& frameToken) {
    if (m_reflexOptionsApplied && m_reflexMode != ReflexMode::Off) {
        return (slReflexSleep(frameToken) == sl::Result::eOk);
    }
    return false;
}

bool DLSSSystem::ConfigureDLSSG(bool enable, uint32_t multiplier, ID3D12CommandQueue* queue) {
    if (!m_dlssGSupported) return false;

    if (!enable) {
        if (m_dlssGConfigured) {
            sl::DLSSGOptions off{};
            off.mode = sl::DLSSGMode::eOff;
            slDLSSGSetOptions(sl::ViewportHandle{0}, off);
            m_dlssGConfigured = false;
        }
        if (queue && m_dlssGFence && m_dlssGFenceValue) {
            queue->Wait(m_dlssGFence.Get(), m_dlssGFenceValue);
        }
        m_dlssGRequested = false;
        return true;
    }

    sl::DLSSGState state{};
    slDLSSGGetState(sl::ViewportHandle{0}, state, nullptr);
    sl::DLSSGOptions options{};
    options.mode = sl::DLSSGMode::eOn;
    options.numFramesToGenerate = (std::min)(multiplier - 1, (std::max)(1u, state.numFramesToGenerateMax));

    sl::Result result = slDLSSGSetOptions(sl::ViewportHandle{0}, options);
    m_dlssGConfigured = (result == sl::Result::eOk);
    m_dlssGRequested = m_dlssGConfigured;
    return m_dlssGConfigured;
}

bool DLSSSystem::TagFrameGenResources(const sl::FrameToken& frameToken,
                                      ID3D12GraphicsCommandList* cmdList,
                                      ID3D12Resource* hudlessColor,
                                      ID3D12Resource* uiColor,
                                      ID3D12Resource* depth,
                                      ID3D12Resource* mvec,
                                      const sl::Constants& constants) {
    if (!m_dlssGRequested || !hudlessColor || !uiColor || !cmdList) return false;

    D3D12_RESOURCE_DESC desc = hudlessColor->GetDesc();
    sl::Extent extent = { 0, 0, uint32_t(desc.Width), uint32_t(desc.Height) };
    sl::Resource resHudless{ sl::ResourceType::eTex2d, hudlessColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource resUI{ sl::ResourceType::eTex2d, uiColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource resDepth{ sl::ResourceType::eTex2d, depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource resMvec{ sl::ResourceType::eTex2d, mvec, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };

    sl::ResourceTag tags[] = {
        sl::ResourceTag(&resHudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &extent),
        sl::ResourceTag(&resUI, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &extent),
        sl::ResourceTag(&resDepth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extent),
        sl::ResourceTag(&resMvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extent)
    };

    if (slSetTagForFrame(frameToken, sl::ViewportHandle{0}, tags, 4, cmdList) != sl::Result::eOk ||
        slSetConstants(constants, frameToken, sl::ViewportHandle{0}) != sl::Result::eOk) {
        m_dlssGRequested = false;
        EngineLog::Write("DLSS-G tagging failed; interpolation disabled");
        return false;
    }
    return true;
}

void DLSSSystem::PollFrameGenState() {
    if (m_dlssGConfigured) {
        sl::DLSSGState state{};
        auto result = slDLSSGGetState(sl::ViewportHandle{0}, state, nullptr);
        m_dlssGActive = (result == sl::Result::eOk && state.status == sl::DLSSGStatus::eOk && state.numFramesActuallyPresented > 1);
        if (m_dlssGActive) {
            m_dlssGeneratedFrames += state.numFramesActuallyPresented - 1;
        }
        m_dlssGFence = static_cast<ID3D12Fence*>(state.inputsProcessingCompletionFence);
        m_dlssGFenceValue = state.lastPresentInputsProcessingCompletionFenceValue;
    } else {
        m_dlssGActive = false;
    }
}

bool DLSSSystem::UpgradeSwapChain(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    auto result = slUpgradeInterface(reinterpret_cast<void**>(&swapChain));
    if (result != sl::Result::eOk) {
        EngineLog::Write("Streamline presentation upgrade failed: " + std::to_string(int(result)));
        return false;
    }
    return true;
}

bool DLSSSystem::Evaluate(const EvaluateParams& p) {
    if (!m_dlssSupported || !p.frameToken || !p.commandList) {
        m_dlssActive = false;
        return false;
    }

    sl::DLSSOptions dlssOpts = {};
    switch (p.quality) {
        case UpscaleQuality::Quality: dlssOpts.mode = sl::DLSSMode::eMaxQuality; break;
        case UpscaleQuality::Balanced: dlssOpts.mode = sl::DLSSMode::eBalanced; break;
        case UpscaleQuality::Performance: dlssOpts.mode = sl::DLSSMode::eMaxPerformance; break;
        case UpscaleQuality::UltraPerformance: dlssOpts.mode = sl::DLSSMode::eUltraPerformance; break;
        case UpscaleQuality::Native: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
        default: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
    }
    dlssOpts.outputWidth = p.targetWidth;
    dlssOpts.outputHeight = p.targetHeight;
    dlssOpts.sharpness = p.sharpness;
    dlssOpts.colorBuffersHDR = sl::Boolean::eTrue;

    // Presets K/M/L
    dlssOpts.dlaaPreset = sl::DLSSPreset::ePresetK;
    dlssOpts.qualityPreset = sl::DLSSPreset::ePresetK;
    dlssOpts.balancedPreset = sl::DLSSPreset::ePresetK;
    dlssOpts.performancePreset = sl::DLSSPreset::ePresetM;
    dlssOpts.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
    dlssOpts.ultraQualityPreset = sl::DLSSPreset::ePresetK;

    sl::Constants slConsts = {};
    slConsts.cameraPos.x = p.cameraPos.x; slConsts.cameraPos.y = p.cameraPos.y; slConsts.cameraPos.z = p.cameraPos.z;
    slConsts.cameraUp.x = p.cameraUp.x; slConsts.cameraUp.y = p.cameraUp.y; slConsts.cameraUp.z = p.cameraUp.z;
    slConsts.cameraRight.x = p.cameraRight.x; slConsts.cameraRight.y = p.cameraRight.y; slConsts.cameraRight.z = p.cameraRight.z;
    slConsts.cameraFwd.x = p.cameraFwd.x; slConsts.cameraFwd.y = p.cameraFwd.y; slConsts.cameraFwd.z = p.cameraFwd.z;

    float fovRad = DirectX::XMConvertToRadians(p.fov);
    float aspect = (p.renderHeight > 0) ? (static_cast<float>(p.renderWidth) / p.renderHeight) : 1.0f;
    memcpy(&slConsts.cameraViewToClip, &p.projMatrix, sizeof(float) * 16);

    sl::recalculateCameraMatrices(slConsts);

    auto toPrevious = DirectX::XMMatrixInverse(nullptr, p.cleanViewProj) * p.prevCleanViewProj;
    auto fromPrevious = DirectX::XMMatrixInverse(nullptr, toPrevious);
    memcpy(&slConsts.clipToPrevClip, &toPrevious, 64);
    memcpy(&slConsts.prevClipToClip, &fromPrevious, 64);

    slConsts.jitterOffset.x = p.jitter.x * p.renderWidth * 0.5f;
    slConsts.jitterOffset.y = -p.jitter.y * p.renderHeight * 0.5f;
    slConsts.cameraPinholeOffset.x = 0;
    slConsts.cameraPinholeOffset.y = 0;
    slConsts.mvecScale.x = 1.0f;
    slConsts.mvecScale.y = 1.0f;
    slConsts.cameraNear = p.nearZ;
    slConsts.cameraFar = p.farZ;
    slConsts.cameraFOV = fovRad;
    slConsts.cameraAspectRatio = aspect;
    slConsts.depthInverted = sl::Boolean::eFalse;
    slConsts.cameraMotionIncluded = sl::Boolean::eTrue;
    slConsts.motionVectors3D = sl::Boolean::eFalse;
    slConsts.reset = (p.reset || m_dlssNeedsReset) ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    sl::ViewportHandle vHandle = { 0 };
    sl::Resource colorRes{ sl::ResourceType::eTex2d, p.inputColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource depthRes{ sl::ResourceType::eTex2d, p.depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource mvecRes{ sl::ResourceType::eTex2d, p.velocity, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
    sl::Resource outputRes{ sl::ResourceType::eTex2d, p.outputColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
    sl::Extent inputExtent = { 0, 0, p.renderWidth, p.renderHeight };
    sl::Extent outputExtent = { 0, 0, p.targetWidth, p.targetHeight };

    sl::ResourceTag tags[] = {
        sl::ResourceTag(&colorRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent),
        sl::ResourceTag(&outputRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent),
        sl::ResourceTag(&depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent),
        sl::ResourceTag(&mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent)
    };

    sl::Result tagResult = slSetTagForFrame(*p.frameToken, vHandle, tags, _countof(tags), p.commandList);
    sl::Result guideResult = sl::Result::eOk;

    if (p.rrRequested && m_dlssRRSupported) {
        sl::Resource diffuse{ sl::ResourceType::eTex2d, p.rrDiffuseAlbedo, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        sl::Resource specular{ sl::ResourceType::eTex2d, p.rrSpecularAlbedo, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        sl::Resource normal{ sl::ResourceType::eTex2d, p.normalTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
        sl::Resource distance{ sl::ResourceType::eTex2d, p.rrHitDistance, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };

        sl::ResourceTag guides[] = {
            { &diffuse, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
            { &specular, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
            { &normal, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent },
            { &distance, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent }
        };
        guideResult = slSetTagForFrame(*p.frameToken, vHandle, guides, 4, p.commandList);
    }

    sl::Result dlssOptionResult = slDLSSSetOptions(vHandle, dlssOpts);
    sl::Result rrOptionResult = sl::Result::eOk;

    if (m_dlssRRSupported) {
        sl::DLSSDOptions dlssdOpts = {};
        dlssdOpts.mode = p.rrRequested ? dlssOpts.mode : sl::DLSSMode::eOff;
        dlssdOpts.outputWidth = p.targetWidth;
        dlssdOpts.outputHeight = p.targetHeight;
        dlssdOpts.sharpness = p.sharpness;
        dlssdOpts.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
        dlssdOpts.colorBuffersHDR = sl::Boolean::eTrue;

        sl::DLSSDPreset rrPreset = (p.rrVersion == DLSSRRVersion::Version1_0) ? sl::DLSSDPreset::ePresetD : sl::DLSSDPreset::ePresetE;
        dlssdOpts.dlaaPreset = rrPreset;
        dlssdOpts.qualityPreset = rrPreset;
        dlssdOpts.balancedPreset = rrPreset;
        dlssdOpts.performancePreset = rrPreset;
        dlssdOpts.ultraPerformancePreset = rrPreset;
        dlssdOpts.ultraQualityPreset = rrPreset;

        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invV = DirectX::XMMatrixInverse(&det, p.viewMatrix);
        DirectX::XMFLOAT4X4 viewMat, invViewMat;
        DirectX::XMStoreFloat4x4(&viewMat, p.viewMatrix);
        DirectX::XMStoreFloat4x4(&invViewMat, invV);
        memcpy(&dlssdOpts.worldToCameraView, &viewMat, sizeof(sl::float4x4));
        memcpy(&dlssdOpts.cameraViewToWorld, &invViewMat, sizeof(sl::float4x4));

        rrOptionResult = slDLSSDSetOptions(vHandle, dlssdOpts);
    }

    sl::Result constantsResult = slSetConstants(slConsts, *p.frameToken, vHandle);
    sl::Feature featureToEval = p.rrRequested ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS;
    const sl::BaseStructure* inputs[] = {
        static_cast<const sl::BaseStructure*>(&vHandle),
        static_cast<const sl::BaseStructure*>(&dlssOpts)
    };

    sl::Result res = tagResult;
    if (res == sl::Result::eOk) res = constantsResult;
    if (res == sl::Result::eOk) res = p.rrRequested ? guideResult : dlssOptionResult;
    if (res == sl::Result::eOk && p.rrRequested) res = rrOptionResult;
    if (res == sl::Result::eOk) {
        res = slEvaluateFeature(featureToEval, *p.frameToken, inputs, p.rrRequested ? 1 : _countof(inputs), p.commandList);
    }

    m_dlssRRActive = (p.rrRequested && res == sl::Result::eOk);
    if (p.rrRequested && res != sl::Result::eOk && tagResult == sl::Result::eOk &&
        constantsResult == sl::Result::eOk && dlssOptionResult == sl::Result::eOk) {
        EngineLog::Write("DLSS RR setup/evaluation failed: " + std::to_string((int)res) + "; falling back to DLSS SR");
        res = slEvaluateFeature(sl::kFeatureDLSS, *p.frameToken, inputs, _countof(inputs), p.commandList);
    }

    if (res == sl::Result::eOk) {
        m_dlssActive = true;
        m_dlssNeedsReset = false;
        return true;
    } else {
        static int errorCount = 0;
        if (errorCount++ % 100 == 0) {
            EngineLog::Write("slEvaluateFeature FAILED with result: " + std::to_string((int)res));
        }
        m_dlssActive = false;
        return false;
    }
}

} // namespace EngineRender
#endif // _WIN32
