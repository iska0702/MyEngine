#pragma once
#if defined(_WIN32)
#include "FidelityFXSystem.h"
#include <wrl/client.h>
#include <atomic>
#include "../../../ThirdParty/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/ffx_framegeneration.h"
namespace EngineRender {
class FidelityFXFrameGeneration {
    HMODULE module=nullptr;
    ffxFunctions api{};
    ffxContext swapContext=nullptr, context=nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> proxy;
    ffxCreateBackendDX12Desc backend{};
    ffxCreateContextDescFrameGeneration create{};
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 wrap{};
    IDXGISwapChain4* wrapped=nullptr;
    DXGI_SWAP_CHAIN_DESC1 swapDescription{};
    Microsoft::WRL::ComPtr<IDXGIFactory> swapFactory;
    std::atomic<uint64_t> generated{0};
    bool enabled=false;
    static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* params,void* user) {
        auto& self=*static_cast<FidelityFXFrameGeneration*>(user);
        auto result=self.api.Dispatch(&self.context,&params->header);
        if(result==FFX_API_RETURN_OK)self.generated+=params->numGeneratedFrames;
        else EngineLog::Write("[FSR FG] Generation failed: "+std::to_string(result));
        return result;
    }
public:
    ~FidelityFXFrameGeneration(){Shutdown();if(module)FreeLibrary(module);}
    bool Initialize(ID3D12Device* device,ID3D12CommandQueue* queue,HWND hwnd,DXGI_SWAP_CHAIN_DESC1& desc,IDXGIFactory* factory,uint32_t width,uint32_t height) {
        if(!module){
            wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            auto path=std::filesystem::path(executable).parent_path()/L"ThirdParty/AMD/amd_fidelityfx_dx12.dll";
            module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!module)return false;ffxLoadFunctions(&api,module);
            if(!api.CreateContext||!api.DestroyContext||!api.Configure||!api.Dispatch)return false;
        }
        backend={};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=device;
        if(!swapContext){

            wrap={};wrap.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
            swapDescription=desc;swapFactory=factory;
            wrap.swapchain=&wrapped;wrap.gameQueue=queue;wrap.hwnd=hwnd;wrap.desc=&swapDescription;wrap.dxgiFactory=swapFactory.Get();
            auto result=api.CreateContext(&swapContext,&wrap.header,nullptr);
            if(result!=FFX_API_RETURN_OK){wrapped=nullptr;EngineLog::Write("[FSR FG] Swapchain creation failed: "+std::to_string(result));return false;}
            proxy.Attach(wrapped);
        }
        if(context&&create.displaySize.width==width&&create.displaySize.height==height)return true;
        Wait();
        if(context)api.DestroyContext(&context,nullptr);
        create={};create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;create.header.pNext=&backend.header;
        create.displaySize={width,height};create.maxRenderSize={width,height};create.backBufferFormat=FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        auto result=api.CreateContext(&context,&create.header,nullptr);
        if(result!=FFX_API_RETURN_OK){context=nullptr;EngineLog::Write("[FSR FG] Context creation failed: "+std::to_string(result));return false;}
        EngineLog::Write("[FSR FG] Interpolation swapchain and context ready (2x).");return true;
    }
    IDXGISwapChain4* SwapChain()const{return proxy.Get();}
    uint64_t GeneratedFrames()const{return generated.load();}
    void Wait(){if(swapContext){ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};wait.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;api.Dispatch(&swapContext,&wait.header);}}
    void Disable(){
        if(context&&enabled){ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;config.swapChain=proxy.Get();config.frameGenerationEnabled=false;api.Configure(&context,&config.header);}
        enabled=false;Wait();
    }
    void Shutdown(){Disable();if(context)api.DestroyContext(&context,nullptr);context=nullptr;proxy.Reset();wrapped=nullptr;if(swapContext)api.DestroyContext(&swapContext,nullptr);swapContext=nullptr;}
    bool Prepare(ID3D12GraphicsCommandList* list,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* hudless,
        uint64_t frame,uint32_t width,uint32_t height,float jitterX,float jitterY,float dt,float fov,
        const float* position,const float* right,const float* up,const float* forward) {
        if(!context)return false;
        ffxConfigureDescFrameGeneration config{};config.header.type=FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        config.swapChain=proxy.Get();config.frameGenerationEnabled=true;
        config.frameGenerationCallback=Generate;config.frameGenerationCallbackUserContext=this;
        config.HUDLessColor=ffxApiGetResourceDX12(hudless,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        config.generationRect={0,0,int32_t(create.displaySize.width),int32_t(create.displaySize.height)};config.frameID=frame;
        if(api.Configure(&context,&config.header)!=FFX_API_RETURN_OK)return false;
        enabled=true;
        ffxDispatchDescFrameGenerationPrepareCameraInfo camera{};camera.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        memcpy(camera.cameraPosition,position,12);memcpy(camera.cameraRight,right,12);memcpy(camera.cameraUp,up,12);memcpy(camera.cameraForward,forward,12);
        ffxDispatchDescFrameGenerationPrepare prepare{};prepare.header.type=FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;prepare.header.pNext=&camera.header;
        prepare.commandList=list;prepare.frameID=frame;prepare.renderSize={width,height};prepare.jitterOffset={jitterX,jitterY};prepare.motionVectorScale={float(width),float(height)};
        prepare.frameTimeDelta=dt*1000;prepare.cameraNear=.1f;prepare.cameraFar=2000;prepare.cameraFovAngleVertical=fov;prepare.viewSpaceToMetersFactor=1;
        prepare.depth=ffxApiGetResourceDX12(depth,FFX_API_RESOURCE_STATE_COMPUTE_READ,FFX_API_RESOURCE_USAGE_DEPTHTARGET);
        prepare.motionVectors=ffxApiGetResourceDX12(motion,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        auto result=api.Dispatch(&context,&prepare.header);
        if(result!=FFX_API_RETURN_OK){EngineLog::Write("[FSR FG] Prepare failed: "+std::to_string(result));Disable();return false;}
        return true;
    }
};
}
#endif
