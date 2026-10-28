#pragma once
#include "Core/Project.h"
#include "Core/EngineLog.h"
#include <d3d12.h>
#include <wrl.h>

namespace AdvancedShaderDelivery {
inline bool identityReady=false;
inline bool defaultDatabaseActive=false;
inline std::string runtimeStatus="Not queried";
inline void ReportStatistics(ID3D12Device* device) {
    if(!device)return;
    Microsoft::WRL::ComPtr<ID3D12DeviceStatistics> query;
    D3D12_STATE_OBJECT_STATISTICS stats{};
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&query)))||FAILED(query->GetStateObjectStatistics(&stats))){
        runtimeStatus="Runtime statistics unavailable";EngineLog::Write("ASD statistics: unavailable");return;
    }
    defaultDatabaseActive=stats.DefaultPSDBRegistered!=FALSE;
    runtimeStatus=defaultDatabaseActive?"Matching default PSDB connected":"No matching default PSDB connected";
    std::ostringstream out;out<<"ASD statistics: defaultPSDB="<<stats.DefaultPSDBRegistered;
    auto append=[&](const char* name,const D3D12_CREATE_STATE_OBJECT_STATISTICS& s){
        out<<"; "<<name<<" created="<<s.NumCreated<<" psdbMiss="<<s.NumPSDBCacheMissed
           <<" totalMiss="<<s.NumTotalCacheMissed<<" unknown="<<s.NumCacheUnknown;
    };
    append("PSO",stats.PipelineStateObjectStatistics);append("SO",stats.StateObjectStatistics);
    EngineLog::Write(out.str());
}
inline UINT64 Version(const std::string& value) {
    UINT64 result=0;std::istringstream input(value);std::string part;
    for(int i=0;i<4;++i){
        unsigned long number=0;
        if(std::getline(input,part,'.')){
            if(part.empty()||part.find_first_not_of("0123456789")!=std::string::npos)return 0;
            try{number=std::stoul(part);}catch(...){return 0;}
            if(number>65535)return 0;
        }
        result=(result<<16)|number;
    }
    if(std::getline(input,part,'.'))return 0;
    return result;
}
// Must precede every device creation, including adapter capability probes.
inline void SetIdentity() {
    if(!EngineProject::active)return;
    const auto& project=EngineProject::current;
    GUID appId{};
    const auto id=EngineProject::Path("{"+project.shaderAppId+"}").wstring();
    if(FAILED(CLSIDFromString(id.c_str(),&appId))){EngineLog::Write("ASD: invalid project application GUID");return;}
    Microsoft::WRL::ComPtr<ID3D12ApplicationIdentity> identity;
    constexpr GUID clsid={0x08d8e1e8,0x75a6,0x42a7,{0xbf,0x3a,0xd0,0x5f,0xe5,0x29,0xc4,0x7c}};
    HRESULT hr=D3D12GetInterface(clsid,IID_PPV_ARGS(&identity));
    if(SUCCEEDED(hr)){
        const auto name=EngineProject::Path(project.name).wstring();
        D3D12_APPLICATION_DESC desc{};
        desc.pExeFilename=L"Idontknow.exe";desc.pName=name.c_str();
        desc.Version.Version=Version(project.version);
        desc.pEngineName=L"Idontknow";desc.EngineVersion.Version=0x0001000000000000ull;
        hr=identity->SetApplicationIdentity(&desc,appId);
    }
    identityReady=SUCCEEDED(hr);
    std::ostringstream message;message<<"ASD: Agility SDK 1.619.6; project identity "
        <<(identityReady?"set":"unavailable")<<"; HRESULT=0x"<<std::hex<<static_cast<unsigned long>(hr)
        <<"; precompiled cache usage requires a matching PSDB registration or developer override";
    EngineLog::Write(message.str());
}
}
