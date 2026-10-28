#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <iostream>
#include "../ThirdParty/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/ffx_api_loader.h"
#include "../ThirdParty/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/ffx_upscale.h"
#include "../ThirdParty/FidelityFX-SDK-v1.1.4/ffx-api/include/ffx_api/dx12/ffx_api_dx12.h"
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;
 auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 if(!module){std::cout<<"load error "<<GetLastError()<<'\n';return 3;}
 ffxFunctions api{};ffxLoadFunctions(&api,module);
 if(!api.CreateContext||!api.Dispatch||!api.DestroyContext)return 4;
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))return 5;
 ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;backend.device=device.Get();
 ffxCreateContextDescUpscale desc{};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;desc.header.pNext=&backend.header;
 desc.flags=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
 desc.maxRenderSize={1280,720};desc.maxUpscaleSize={1280,720};
 ffxContext context=nullptr;auto result=api.CreateContext(&context,&desc.header,nullptr);
 std::cout<<"FSR create "<<result<<" context "<<(context!=nullptr)<<'\n';
 if(context&&api.Query){ffxQueryGetProviderVersion version{};version.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
     auto queried=api.Query(&context,&version.header);std::cout<<"provider_query "<<queried<<" name "<<(version.versionName?version.versionName:"unknown")<<'\n';}
 if(context)api.DestroyContext(&context,nullptr);
 FreeLibrary(module);return result==0?0:6;
}
