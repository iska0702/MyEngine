#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <d3d12.h>
#include <filesystem>
#include <string>
#include "Core/EngineLog.h"
#include "../../../ThirdParty/AMD/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/ffx_api_loader.h"
#include "../../../ThirdParty/AMD/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/ffx_upscale.h"
#include "../../../ThirdParty/AMD/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/dx12/ffx_api_dx12.h"
namespace EngineRender {
class FidelityFXSystem {
    HMODULE module=nullptr;
    ffxFunctions api{};
    ffxContext context=nullptr;
    ffxCreateBackendDX12Desc backend{};
    ffxCreateContextDescUpscale create{};
    bool needsReset=true;
    std::string provider="Unavailable";
    static void Message(uint32_t type,const wchar_t* text) {
        if(!text)return;
        int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
        std::string message(size,'\0');
        WideCharToMultiByte(CP_UTF8,0,text,-1,message.data(),size,nullptr,nullptr);
        if(!message.empty())message.pop_back();
        EngineLog::Write(std::string(type==FFX_API_MESSAGE_TYPE_ERROR?"[FSR ERROR] ":"[FSR] ")+message);
    }
public:
    ~FidelityFXSystem(){Shutdown();if(module)FreeLibrary(module);}
    bool Initialize(ID3D12Device* device,uint32_t width,uint32_t height) {
        Shutdown();
        if(!module){
            wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            auto directory=std::filesystem::path(executable).parent_path()/L"ThirdParty"/L"AMD";
            for(auto name:{L"amd_fidelityfx_upscaler_dx12.dll",L"amd_fidelityfx_dx12.dll"}) {
                module=LoadLibraryExW((directory/name).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
                if(module){ffxLoadFunctions(&api,module);if(api.CreateContext&&api.DestroyContext&&api.Dispatch&&api.Query)break;FreeLibrary(module);module=nullptr;}
            }
        }
        if(!module)return false;
        backend={};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=device;
        create={};create.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;create.header.pNext=&backend.header;
        create.flags=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        create.maxRenderSize={width,height};create.maxUpscaleSize={width,height};create.fpMessage=Message;
        const auto result=api.CreateContext(&context,&create.header,nullptr);
        if(result!=FFX_API_RETURN_OK){Message(0,L"Context creation failed");context=nullptr;return false;}
        ffxQueryGetProviderVersion version{};version.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if(api.Query(&context,&version.header)==FFX_API_RETURN_OK&&version.versionName)provider=version.versionName;
        else provider="AMD FidelityFX temporal upscaler";
        needsReset=true;EngineLog::Write("[FSR] Ready: "+provider);return true;
    }
    void Shutdown(){if(context&&api.DestroyContext)api.DestroyContext(&context,nullptr);context=nullptr;needsReset=true;}
    bool IsReady()const{return context!=nullptr;}
    const std::string& Provider()const{return provider;}
    bool Execute(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* velocity,
        ID3D12Resource* output,uint32_t width,uint32_t height,uint32_t outWidth,uint32_t outHeight,
        float jitterX,float jitterY,float dt,float fov,float sharpness) {
        if(!context||!color||!depth||!velocity||!output)return false;
        ffxDispatchDescUpscale dispatch{};dispatch.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList=list;
        dispatch.color=ffxApiGetResourceDX12(color,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatch.depth=ffxApiGetResourceDX12(depth,FFX_API_RESOURCE_STATE_COMPUTE_READ,FFX_API_RESOURCE_USAGE_DEPTHTARGET);
        dispatch.motionVectors=ffxApiGetResourceDX12(velocity,FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dispatch.output=ffxApiGetResourceDX12(output,FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.jitterOffset={jitterX,jitterY};dispatch.motionVectorScale={float(width),float(height)};
        dispatch.renderSize={width,height};dispatch.upscaleSize={outWidth,outHeight};
        dispatch.enableSharpening=sharpness>0;dispatch.sharpness=sharpness;
        dispatch.frameTimeDelta=dt*1000;dispatch.preExposure=1;dispatch.reset=needsReset;
        dispatch.cameraNear=.1f;dispatch.cameraFar=2000;dispatch.cameraFovAngleVertical=fov;dispatch.viewSpaceToMetersFactor=1;
        const auto result=api.Dispatch(&context,&dispatch.header);
        if(result==FFX_API_RETURN_OK){needsReset=false;return true;}
        EngineLog::Write("[FSR ERROR] Dispatch returned "+std::to_string(result));needsReset=true;return false;
    }
};
}
#endif
