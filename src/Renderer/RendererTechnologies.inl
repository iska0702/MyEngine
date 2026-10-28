// Windows renderer SDK integration. Included by Renderer.cpp so build targets
// share exactly the same implementation without an additional translation unit.

bool Renderer::InitFSR2(){
    if(!m_fidelityFX)m_fidelityFX=std::make_unique<EngineRender::FidelityFXSystem>();
    m_fsr2Initialized=m_fidelityFX->Initialize(m_device.Get(),m_width,m_height);
    m_fsrSupported=m_fsr2Initialized;return m_fsr2Initialized;
}
void Renderer::ShutdownFSR2(){if(m_fidelityFX)m_fidelityFX->Shutdown();m_fsr2Initialized=false;m_fsrActive=false;}
bool Renderer::ApplyFSR2(ID3D12GraphicsCommandList* list){
    if(!m_fsr2Initialized||!m_fidelityFX)return false;
    bool composite=(m_ssaoEnabled||(m_dxrEnabled&&m_dxrDevice))&&m_compositedColorTexture;
    auto* color=composite?m_compositedColorTexture.Get():m_sceneColorTexture.Get();auto& state=composite?m_compositedColorState:m_sceneColorState;
    Transition(list,color,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_velocityTexture.Get(),m_velocityState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_upscaledColorTexture.Get(),m_upscaledColorState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_fsrActive=m_fidelityFX->Execute(list,color,m_depthStencilBuffer.Get(),m_velocityTexture.Get(),m_upscaledColorTexture.Get(),m_renderWidth,m_renderHeight,m_width,m_height,m_camera->GetJitterX()*m_renderWidth*.5f,-m_camera->GetJitterY()*m_renderHeight*.5f,m_lastDt,XMConvertToRadians(m_fov),m_upscaleSharpness);
    return m_fsrActive;
}
bool Renderer::InitXeSS(){
    m_xessSystem=std::make_unique<EngineRender::XeSSSystem>();
    m_xessSupported=m_xessSystem->Initialize(m_device.Get(),m_width,m_height,EngineRender::XeSSSystem::EngineQualityToXeSS(int(m_upscaleQuality)));
    m_xessSystem->InitializeLowLatency(m_device.Get(),m_xellEnabled);m_xellSupported=m_xessSystem->IsLowLatencySupported();return m_xessSupported;
}
void Renderer::ShutdownXeSS(){if(m_xessSystem)m_xessSystem->Shutdown();m_xessSystem.reset();m_xessSupported=false;m_xessActive=false;}
void Renderer::ApplyXeSS(ID3D12GraphicsCommandList* list){
    if(!m_xessSystem||!m_xessSystem->IsInitialized())return;
    bool composite=(m_ssaoEnabled||(m_dxrEnabled&&m_dxrDevice))&&m_compositedColorTexture;
    auto* color=composite?m_compositedColorTexture.Get():m_sceneColorTexture.Get();auto& state=composite?m_compositedColorState:m_sceneColorState;
    Transition(list,color,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,m_velocityTexture.Get(),m_velocityState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,m_upscaledColorTexture.Get(),m_upscaledColorState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_xessActive=m_xessSystem->ExecuteSR(list,color,m_velocityTexture.Get(),m_depthStencilBuffer.Get(),m_upscaledColorTexture.Get(),m_camera->GetJitterX()*m_renderWidth*.5f,-m_camera->GetJitterY()*m_renderHeight*.5f,m_xessNeedsReset||m_firstFrameAfterResize);
    if(m_xessActive)m_xessNeedsReset=false;
}
bool Renderer::InitDirectStorage(){m_directStorage=std::make_unique<EngineStorage::DirectStorageSystem>();return m_directStorage->Initialize(m_device.Get());}
void Renderer::ShutdownDirectStorage(){m_directStorage.reset();}
void Renderer::SetReflexMode(ReflexMode mode){
    m_reflexMode=mode;m_reflexOptionsApplied=false;
    if(mode!=ReflexMode::Off&&m_xellEnabled)SetXeLLEnabled(false);
    if(m_reflexSupported){sl::ReflexOptions options{};options.mode=static_cast<sl::ReflexMode>(int(mode));m_reflexOptionsApplied=slReflexSetOptions(options)==sl::Result::eOk;}
}
void Renderer::SetXeLLEnabled(bool enabled,bool boost){
    if(!m_xessSystem){m_xellEnabled=false;return;}
    if(enabled&&!m_xessSystem->IsLowLatencySupported())m_xessSystem->InitializeLowLatency(m_device.Get(),true,boost);
    else m_xessSystem->SetLowLatencyMode(enabled,boost);
    m_xellSupported=m_xessSystem->IsLowLatencySupported();m_xellEnabled=m_xessSystem->IsLowLatencyEnabled();
    if(m_xellEnabled)SetReflexMode(ReflexMode::Off);
}
void Renderer::SetXeSSFrameGenEnabled(bool enabled){m_xessFrameGenEnabled=enabled;if(enabled){m_fsrFrameGenEnabled=false;m_dlssGRequested=false;}}
void Renderer::SynchronizeForUpdate(){
    if(!m_device||m_deviceRemoved)return;
    if(!s_frameSynchronized){auto start=std::chrono::steady_clock::now();WaitForPreviousFrame();m_lastGpuWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();s_frameSynchronized=true;}
}
void Renderer::BeginSimulationFrame(){
    if(m_xessFrameGenEnabled&&!m_xellEnabled)SetXeLLEnabled(true);
    if(m_dlssGRequested&&m_reflexMode==ReflexMode::Off)SetReflexMode(ReflexMode::LowLatency);
    m_latencyFrameId=uint32_t(m_frameCounter+1);m_slFrameToken=nullptr;
    if(m_streamlineInitialized)slGetNewFrameToken(m_slFrameToken);
    m_reflexFrameValid=false;m_xellFrameValid=false;
    if(m_reflexOptionsApplied&&m_reflexMode!=ReflexMode::Off&&m_slFrameToken)m_reflexFrameValid=slReflexSleep(*m_slFrameToken)==sl::Result::eOk;
    if(m_xellEnabled&&m_xessSystem)m_xellFrameValid=m_xessSystem->SleepLowLatency(m_latencyFrameId);
    MarkLatencyStage(0);
    if(m_xellFrameValid)m_xellFrameValid=m_xessSystem->AddLatencyMarker(m_latencyFrameId,XELL_INPUT_SAMPLE)&&m_xellFrameValid;
}
void Renderer::MarkLatencyStage(int stage){
    if(m_reflexFrameValid&&m_slFrameToken)m_reflexFrameValid=slPCLSetMarker(static_cast<sl::PCLMarker>(stage),*m_slFrameToken)==sl::Result::eOk;
    if(m_xellFrameValid&&m_xessSystem)m_xellFrameValid=m_xessSystem->AddLatencyMarker(m_latencyFrameId,static_cast<xell_latency_marker_type_t>(stage));
    if(stage==5){m_reflexActive=m_reflexFrameValid;m_xellActive=m_xellFrameValid;}
}
void Renderer::AttachStreamlinePresentation(){
    if(!m_streamlineInitialized||!m_swapChain)return;
    auto* swap=m_swapChain.Detach();auto* original=swap;auto result=slUpgradeInterface(reinterpret_cast<void**>(&swap));if(swap!=original)original->Release();m_swapChain.Attach(swap);
    if(result!=sl::Result::eOk)LOG("Streamline presentation upgrade failed: "<<int(result));
}

void Renderer::UpdateTechnologySwapChain(){
    int requested=m_fsrFrameGenEnabled?1:m_xessFrameGenEnabled?2:m_dlssGRequested?3:0;
    const bool replace=(requested&&requested!=m_frameGenProxyProvider)||(m_frameGenProxyProvider==3&&!requested);
    if(replace){
        DXGI_SWAP_CHAIN_DESC1 desc{};m_swapChain->GetDesc1(&desc);
        ComPtr<IDXGIFactory2> factory;CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));
        if(m_fidelityFXFrameGeneration)m_fidelityFXFrameGeneration->Disable();
        if(m_xessSystem&&m_frameGenProxyProvider==2)m_xessSystem->SetFrameGenEnabled(false);
        if(m_dlssGConfigured){sl::DLSSGOptions off{};off.mode=sl::DLSSGMode::eOff;slDLSSGSetOptions(sl::ViewportHandle{0},off);m_dlssGConfigured=false;}
        if(m_dlssGFence&&m_dlssGFenceValue)m_commandQueue->Wait(m_dlssGFence.Get(),m_dlssGFenceValue);
        WaitForPreviousFrame();for(auto& target:m_renderTargets)target.Reset();m_swapChain.Reset();
        m_fidelityFXFrameGeneration.reset();if(m_xessSystem)m_xessSystem->ShutdownFrameGeneration();m_frameGenProxyProvider=0;
        if(requested==1){
            m_fidelityFXFrameGeneration=std::make_unique<EngineRender::FidelityFXFrameGeneration>();
            bool ready=m_fidelityFXFrameGeneration->Initialize(m_device.Get(),m_commandQueue.Get(),m_hwnd,desc,factory.Get(),m_width,m_height);
            if(auto* swap=m_fidelityFXFrameGeneration->SwapChain())swap->QueryInterface(IID_PPV_ARGS(&m_swapChain));
            m_fsrFrameGenEnabled=ready;if(m_swapChain)m_frameGenProxyProvider=1;
        }else if(requested==2&&m_xessSystem){
            SetXeLLEnabled(true);
            if(m_xessSystem->InitializeFrameGenerationForWindow(m_device.Get(),m_commandQueue.Get(),m_hwnd,desc,factory.Get())){m_xessSystem->GetFGSwapChain()->QueryInterface(IID_PPV_ARGS(&m_swapChain));m_frameGenProxyProvider=2;}
            else m_xessFrameGenEnabled=false;
        }else if(requested==3){
            SetReflexMode(ReflexMode::LowLatency);
            auto* upgraded=factory.Detach();auto* native=upgraded;auto result=slUpgradeInterface(reinterpret_cast<void**>(&upgraded));if(upgraded!=native)native->Release();factory.Attach(upgraded);
            sl::DLSSGState state{};slDLSSGGetState(sl::ViewportHandle{0},state,nullptr);
            sl::DLSSGOptions options{};options.mode=sl::DLSSGMode::eOn;options.numFramesToGenerate=(std::min)(uint32_t(m_dlssGMultiplier-1),(std::max)(1u,state.numFramesToGenerateMax));
            m_dlssGConfigured=result==sl::Result::eOk&&slDLSSGSetOptions(sl::ViewportHandle{0},options)==sl::Result::eOk;m_dlssGRequested=m_dlssGConfigured;m_frameGenProxyProvider=3;
        }
        if(!m_swapChain){ComPtr<IDXGISwapChain1> swap;if(FAILED(factory->CreateSwapChainForHwnd(m_commandQueue.Get(),m_hwnd,&desc,nullptr,nullptr,&swap)))throw std::runtime_error("Cannot create presentation swapchain");swap.As(&m_swapChain);}
        AttachStreamlinePresentation();
        auto handle=m_rtvHeap->GetCPUDescriptorHandleForHeapStart();for(UINT n=0;n<FrameCount;++n){if(FAILED(m_swapChain->GetBuffer(n,IID_PPV_ARGS(&m_renderTargets[n]))))throw std::runtime_error("Cannot acquire presentation buffer");m_device->CreateRenderTargetView(m_renderTargets[n].Get(),nullptr,handle);handle.ptr+=m_rtvDescriptorSize;}
        m_backBufferState=D3D12_RESOURCE_STATE_PRESENT;
    }
    if(m_frameGenProxyProvider==1&&m_fidelityFXFrameGeneration){
        if(m_fsrFrameGenEnabled){DXGI_SWAP_CHAIN_DESC1 desc{};m_swapChain->GetDesc1(&desc);ComPtr<IDXGIFactory2> factory;CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));m_fsrFrameGenEnabled=m_fidelityFXFrameGeneration->Initialize(m_device.Get(),m_commandQueue.Get(),m_hwnd,desc,factory.Get(),m_width,m_height);}
        else m_fidelityFXFrameGeneration->Disable();
    }
    if(m_frameGenProxyProvider==2&&m_xessSystem)m_xessSystem->SetFrameGenEnabled(m_xessFrameGenEnabled);
    if(m_fsrFrameGenEnabled||m_xessFrameGenEnabled||m_dlssGRequested){
        auto desc=m_renderTargets[0]->GetDesc();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        if(!m_frameGenHudless||m_frameGenHudless->GetDesc().Width!=desc.Width||m_frameGenHudless->GetDesc().Height!=desc.Height){
            if(m_fidelityFXFrameGeneration)m_fidelityFXFrameGeneration->Wait();m_frameGenHudless.Reset();desc.Flags=D3D12_RESOURCE_FLAG_NONE;m_frameGenHudlessState=D3D12_RESOURCE_STATE_COPY_DEST;
            if(FAILED(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,m_frameGenHudlessState,nullptr,IID_PPV_ARGS(&m_frameGenHudless))))throw std::runtime_error("Cannot allocate HUD-less image");
        }
        if(m_dlssGRequested&&(!m_frameGenUI||m_frameGenUI->GetDesc().Width!=desc.Width||m_frameGenUI->GetDesc().Height!=desc.Height)){
            m_frameGenUI.Reset();desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;m_frameGenUIState=D3D12_RESOURCE_STATE_RENDER_TARGET;
            if(FAILED(m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,m_frameGenUIState,nullptr,IID_PPV_ARGS(&m_frameGenUI))))throw std::runtime_error("Cannot allocate frame generation UI image");
            auto handle=m_rtvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=31*m_rtvDescriptorSize;m_device->CreateRenderTargetView(m_frameGenUI.Get(),nullptr,handle);
        }
    }
    if(m_dlssGFence&&m_dlssGFenceValue)m_commandQueue->Wait(m_dlssGFence.Get(),m_dlssGFenceValue);
    m_fsrFrameGenActive=false;m_xessFrameGenActive=false;
}

void Renderer::PrepareTechnologyFrame(float dt){
    if(!(m_fsrFrameGenEnabled||m_xessFrameGenEnabled||m_dlssGRequested)||!m_frameGenHudless)return;
    auto* list=m_commandList.Get();if(m_fidelityFXFrameGeneration)m_fidelityFXFrameGeneration->Wait();
    Transition(list,m_renderTargets[m_frameIndex].Get(),m_backBufferState,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(list,m_frameGenHudless.Get(),m_frameGenHudlessState,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(m_frameGenHudless.Get(),m_renderTargets[m_frameIndex].Get());
    Transition(list,m_frameGenHudless.Get(),m_frameGenHudlessState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,m_velocityTexture.Get(),m_velocityState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(m_fsrFrameGenEnabled){
        XMFLOAT4X4 inverse;XMStoreFloat4x4(&inverse,XMMatrixInverse(nullptr,m_camera->GetViewMatrix()));
        m_fsrFrameGenActive=m_fidelityFXFrameGeneration->Prepare(list,m_depthStencilBuffer.Get(),m_velocityTexture.Get(),m_frameGenHudless.Get(),m_frameCounter,m_renderWidth,m_renderHeight,m_camera->GetJitterX()*m_renderWidth*.5f,-m_camera->GetJitterY()*m_renderHeight*.5f,dt,XMConvertToRadians(m_fov),&inverse._41,&inverse._11,&inverse._21,&inverse._31);
        if(!m_fsrFrameGenActive)m_fsrFrameGenEnabled=false;
    }
    if(m_xessFrameGenEnabled){
        xefg_swapchain_frame_constant_data_t constants{};auto view=m_camera->GetViewMatrix();auto projection=m_camera->GetCleanProjectionMatrix(XMConvertToRadians(m_fov),float(m_renderWidth)/m_renderHeight,.1f,2000);
        memcpy(constants.viewMatrix,&view,64);memcpy(constants.projectionMatrix,&projection,64);constants.jitterOffsetX=m_camera->GetJitterX()*m_renderWidth*.5f;constants.jitterOffsetY=-m_camera->GetJitterY()*m_renderHeight*.5f;
        constants.motionVectorScaleX=float(m_renderWidth);constants.motionVectorScaleY=float(m_renderHeight);constants.frameRenderTime=dt*1000;constants.resetHistory=m_xessNeedsReset||m_frameCounter<=2;
        bool valid=m_xessSystem->TagFrameConstantsFG(uint32_t(m_frameCounter),constants);
        auto tag=[&](xefg_swapchain_resource_type_t type,ID3D12Resource* resource,UINT width,UINT height){xefg_swapchain_d3d12_resource_data_t data{};data.type=type;data.validity=XEFG_SWAPCHAIN_RV_ONLY_NOW;data.pResource=resource;data.resourceSize={width,height};data.incomingState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;valid=m_xessSystem->TagFrameResourceFG(list,uint32_t(m_frameCounter),data)&&valid;};
        tag(XEFG_SWAPCHAIN_RES_DEPTH,m_depthStencilBuffer.Get(),m_renderWidth,m_renderHeight);tag(XEFG_SWAPCHAIN_RES_MOTION_VECTOR,m_velocityTexture.Get(),m_renderWidth,m_renderHeight);tag(XEFG_SWAPCHAIN_RES_HUDLESS_COLOR,m_frameGenHudless.Get(),m_width,m_height);
        m_xessFrameGenActive=valid;
        if(valid)m_xessNeedsReset=false;
        else{m_xessFrameGenEnabled=false;m_xessSystem->SetFrameGenEnabled(false);LOG("XeFG tagging failed; interpolation disabled");}
    }
    Transition(list,m_renderTargets[m_frameIndex].Get(),m_backBufferState,D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Renderer::FinishTechnologyPresent(){
    if(m_xessFrameGenActive&&m_xessSystem){auto state=m_xessSystem->GetLastPresentStatusFG();m_xessFrameGenActive=state.frameGenResult==XEFG_SWAPCHAIN_RESULT_SUCCESS&&state.isFrameGenEnabled&&state.framesPresented>1;if(m_xessFrameGenActive)m_xessGeneratedFrames+=state.framesPresented-1;}
    if(m_dlssGConfigured){sl::DLSSGState state{};auto result=slDLSSGGetState(sl::ViewportHandle{0},state,nullptr);m_dlssGActive=result==sl::Result::eOk&&state.status==sl::DLSSGStatus::eOk&&state.numFramesActuallyPresented>1;if(m_dlssGActive)m_dlssGeneratedFrames+=state.numFramesActuallyPresented-1;m_dlssGFence=static_cast<ID3D12Fence*>(state.inputsProcessingCompletionFence);m_dlssGFenceValue=state.lastPresentInputsProcessingCompletionFenceValue;}else m_dlssGActive=false;
}

void Renderer::TagDLSSGFrame(float dt){
    if(!m_dlssGRequested||!m_frameGenUI||!m_frameGenHudless||!m_slFrameToken)return;
    auto* list=m_commandList.Get();
    Transition(list,m_frameGenUI.Get(),m_frameGenUIState,D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto handle=m_rtvHeap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=31*m_rtvDescriptorSize;const float transparent[4]={0,0,0,0};list->ClearRenderTargetView(handle,transparent,0,nullptr);
#ifndef ENGINE_RELEASE
    if(m_imguiInitialized){list->OMSetRenderTargets(1,&handle,FALSE,nullptr);ID3D12DescriptorHeap* heaps[]={m_imguiSrvHeap.Get()};list->SetDescriptorHeaps(1,heaps);ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),list);}
#endif
    Transition(list,m_frameGenUI.Get(),m_frameGenUIState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(list,m_velocityTexture.Get(),m_velocityState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    sl::Constants constants{};auto position=m_camera->GetPosition();auto right=m_camera->GetRightVector();auto up=m_camera->GetUpVector();auto forward=m_camera->GetForwardVector();
    constants.cameraPos={position.x,position.y,position.z};constants.cameraRight={right.x,right.y,right.z};constants.cameraUp={up.x,up.y,up.z};constants.cameraFwd={forward.x,forward.y,forward.z};
    auto projection=m_camera->GetCleanProjectionMatrix(XMConvertToRadians(m_fov),float(m_renderWidth)/m_renderHeight,.1f,2000);
    memcpy(&constants.cameraViewToClip,&projection,64);sl::recalculateCameraMatrices(constants);
    auto toPrevious=XMMatrixInverse(nullptr,m_cleanViewProj)*m_prevCleanViewProj;auto fromPrevious=XMMatrixInverse(nullptr,toPrevious);
    memcpy(&constants.clipToPrevClip,&toPrevious,64);memcpy(&constants.prevClipToClip,&fromPrevious,64);
    constants.jitterOffset={m_camera->GetJitterX()*m_renderWidth*.5f,-m_camera->GetJitterY()*m_renderHeight*.5f};constants.mvecScale={1,1};
    constants.cameraNear=.1f;constants.cameraFar=2000;constants.cameraFOV=XMConvertToRadians(m_fov);constants.cameraAspectRatio=float(m_renderWidth)/m_renderHeight;constants.depthInverted=sl::Boolean::eFalse;constants.cameraMotionIncluded=sl::Boolean::eTrue;constants.motionVectors3D=sl::Boolean::eFalse;constants.reset=m_frameCounter<=2?sl::Boolean::eTrue:sl::Boolean::eFalse;
    sl::Resource depth{sl::ResourceType::eTex2d,m_depthStencilBuffer.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},motion{sl::ResourceType::eTex2d,m_velocityTexture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},hudless{sl::ResourceType::eTex2d,m_frameGenHudless.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},ui{sl::ResourceType::eTex2d,m_frameGenUI.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    sl::Extent input{0,0,uint32_t(m_renderWidth),uint32_t(m_renderHeight)},output{0,0,uint32_t(m_width),uint32_t(m_height)};
    sl::ResourceTag tags[]={{&depth,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&input},{&motion,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&input},{&hudless,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&output},{&ui,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent,&output}};
    if(slSetTagForFrame(*m_slFrameToken,sl::ViewportHandle{0},tags,4,list)!=sl::Result::eOk||slSetConstants(constants,*m_slFrameToken,sl::ViewportHandle{0})!=sl::Result::eOk){m_dlssGRequested=false;LOG("DLSS-G tagging failed; interpolation disabled");}
}

void Renderer::WriteTechnologyState(const std::string& path){
    std::ofstream state(path+".txt");
    state<<"antialiasing technique "<<int(m_upscaleTechnique)<<" quality "<<int(m_upscaleQuality)<<" taa "<<m_taaEnabled<<" dlss_active "<<m_dlssActive<<" xess_active "<<m_xessActive<<" render "<<m_renderWidth<<'x'<<m_renderHeight<<" display "<<m_width<<'x'<<m_height<<'\n';
    state<<"render_backend mesh "<<m_meshShadersAvailable<<" dxr_capability "<<m_dxrAvailable<<" mesh_dispatches "<<m_meshDispatches<<" vertex_draws "<<m_vertexDraws<<" compute_dispatches "<<m_computeDispatches<<" depth_mips "<<m_depthPyramidDispatches<<'\n';
    state<<"pipeline_cache hits "<<m_pipelineCache.hits<<" misses "<<m_pipelineCache.misses<<" failed "<<m_pipelineCache.failed<<'\n';
    state<<"gpu_pass_ms";for(auto value:m_gpuPassTimes)state<<' '<<value;state<<'\n';
    state<<"upscaler_status dlss_supported "<<m_dlssSupported<<" fsr_temporal_active "<<m_fsrActive<<" xess_supported "<<m_xessSupported<<" dlss_fg_hardware "<<m_dlssGSupported<<" virtual_texturing_integrated "<<IsVirtualTexturingIntegrated()<<" frame_generation_integrated "<<IsFrameGenerationIntegrated()<<" ray_reconstruction_integrated "<<IsRayReconstructionIntegrated()<<'\n';
    state<<"fsr_provider "<<(m_fidelityFX?m_fidelityFX->Provider():"unavailable")<<'\n';
    state<<"fsr_frame_generation enabled "<<m_fsrFrameGenEnabled<<" active "<<m_fsrFrameGenActive<<" generated "<<(m_fidelityFXFrameGeneration?m_fidelityFXFrameGeneration->GeneratedFrames():0)<<'\n';
    state<<"xess_frame_generation enabled "<<m_xessFrameGenEnabled<<" active "<<m_xessFrameGenActive<<" generated "<<m_xessGeneratedFrames<<'\n';
    state<<"dlss_frame_generation requested "<<m_dlssGRequested<<" active "<<m_dlssGActive<<" generated "<<m_dlssGeneratedFrames<<'\n';
    state<<"ray_reconstruction supported "<<m_dlssRRSupported<<" active "<<m_dlssRRActive<<" version "<<int(m_dlssRRVersion)<<'\n';
    state<<"latency reflex_supported "<<m_reflexSupported<<" reflex_active "<<m_reflexActive<<" reflex_mode "<<int(m_reflexMode)<<" xell_supported "<<m_xellSupported<<" xell_enabled "<<m_xellEnabled<<" xell_active "<<m_xellActive<<'\n';
    state<<"content_io directstorage_reads "<<EngineStorage::directStorageReads<<" bytes "<<EngineStorage::directStorageBytes<<" gdeflate_textures "<<EngineStorage::streamingTextureReads<<" decoded_bytes "<<EngineStorage::gdeflateDecodedBytes<<'\n';
    state<<"storage "<<(m_directStorage?m_directStorage->GetStatusSummary():"offline")<<'\n';
    state<<"graphics dxr_enabled "<<m_dxrEnabled<<" path_tracing "<<m_pathTracingEnabled<<'\n';
}
