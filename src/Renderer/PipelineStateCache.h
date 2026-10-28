#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <atomic>
#include "Core/Project.h"
#include "Core/EngineLog.h"
namespace EngineRender {
class PipelineStateCache {
    using Clock=std::chrono::steady_clock;
    static constexpr size_t Limit=256ull*1024*1024;
    static uint64_t Hash(const void* bytes,size_t size,uint64_t hash=14695981039346656037ull){
        auto p=static_cast<const unsigned char*>(bytes);for(size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}return hash;
    }
    static std::string Hex(uint64_t v){std::ostringstream s;s<<std::hex<<v;return s.str();}
    std::vector<char> m_seed; // Must outlive the library's retained input pointer.
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary1> m_library;
    ID3D12Device2* m_device=nullptr;
    std::unordered_map<ID3D12RootSignature*,uint64_t> m_roots;
    std::filesystem::path m_file;
    HANDLE m_lock=INVALID_HANDLE_VALUE;
    std::mutex m_mutex;
    bool m_dirty=false;
    uint64_t m_build=0;
    inline static std::atomic<PipelineStateCache*> active{nullptr};
    std::wstring Key(const char* name,ID3D12RootSignature* root,std::initializer_list<D3D12_SHADER_BYTECODE> shaders){
        uint64_t h=Hash(name,strlen(name),m_build);auto it=m_roots.find(root);
        const uint64_t r=it==m_roots.end()?0:it->second;h=Hash(&r,sizeof(r),h);
        for(auto& shader:shaders){h=Hash(&shader.BytecodeLength,sizeof(shader.BytecodeLength),h);if(shader.pShaderBytecode)h=Hash(shader.pShaderBytecode,shader.BytecodeLength,h);}
        auto key=std::string(name)+"-"+Hex(h);return {key.begin(),key.end()};
    }
public:
    static HRESULT GraphicsForDevice(ID3D12Device* device,const char* name,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** out){
        auto cache=active.load();
        if(cache&&static_cast<ID3D12Device*>(cache->m_device)==device)return cache->Graphics(name,desc,iid,out);
        return device->CreateGraphicsPipelineState(desc,iid,out);
    }
    struct Entry {std::string name,kind;bool hit=false;HRESULT result=S_OK;double milliseconds=0;};
    std::vector<Entry> entries;
    uint32_t hits=0,misses=0,failed=0,storeFailed=0;
    const std::filesystem::path& Path()const{return m_file;}
    bool Available()const{return bool(m_library);}
    void RegisterRoot(ID3D12RootSignature* root,const void* bytes,size_t length){
        std::lock_guard lock(m_mutex);
        if(root)m_roots[root]=Hash(bytes,length);
    }
    void Initialize(ID3D12Device2* device){
        Shutdown();hits=misses=failed=storeFailed=0;
        m_device=device;
        active.store(this);
        // ASD coverage checks must call Create* instead of reusing this library.
        if(GetEnvironmentVariableW(L"IDK_ASD_VERIFICATION",nullptr,0)>0){
            EngineLog::Write("Pipeline cache: bypassed for ASD verification");return;
        }
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        std::ifstream input(exe,std::ios::binary);char block[65536];m_build=14695981039346656037ull;
        while(input){input.read(block,sizeof(block));m_build=Hash(block,size_t(input.gcount()),m_build);}
        wchar_t local[32768]{};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))return;
        auto luid=device->GetAdapterLuid();
        m_file=std::filesystem::path(local)/"Idontknow/ShaderCache"/(EngineProject::active?EngineProject::current.shaderAppId:"default")/
            (Hex(uint32_t(luid.HighPart))+"-"+Hex(luid.LowPart))/(Hex(m_build)+".pso");
        std::error_code ec;std::filesystem::create_directories(m_file.parent_path(),ec);if(ec)return;
        auto lock=m_file;lock+=L".lock";
        m_lock=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        D3D12_FEATURE_DATA_SHADER_CACHE support{};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE,&support,sizeof(support)))||!(support.SupportFlags&D3D12_SHADER_CACHE_SUPPORT_LIBRARY)){
            EngineLog::Write("Pipeline cache: driver library unsupported; direct creation remains active");return;
        }
        auto size=std::filesystem::file_size(m_file,ec);
        if(!ec&&size>0&&size<=Limit){
            m_seed.resize(size);std::ifstream file(m_file,std::ios::binary);file.read(m_seed.data(),m_seed.size());if(!file)m_seed.clear();
        }
        HRESULT hr=device->CreatePipelineLibrary(m_seed.empty()?nullptr:m_seed.data(),m_seed.size(),IID_PPV_ARGS(&m_library));
        if(FAILED(hr)){
            EngineLog::Write("Pipeline cache: incompatible/corrupt library discarded; rebuilding");
            m_library.Reset();m_seed.clear();device->CreatePipelineLibrary(nullptr,0,IID_PPV_ARGS(&m_library));
        }
        EngineLog::Write("Pipeline cache file: "+EngineProject::Utf8(m_file));
    }
    template<class Load,class Create>
    HRESULT Acquire(const char* name,const char* kind,const std::wstring& key,REFIID iid,void** output,Load load,Create create){
        const auto start=Clock::now();*output=nullptr;
        bool hit=false;HRESULT hr=E_FAIL;
        if(m_library){hr=load();hit=SUCCEEDED(hr);}
        if(hit)++hits;
        else{
            ++misses;hr=create();
            if(SUCCEEDED(hr)&&m_library){
                Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
                if(SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&pso)))){
                    auto stored=m_library->StorePipeline(key.c_str(),pso.Get());
                    if(SUCCEEDED(stored))m_dirty=true;else ++storeFailed;
                }
            }
        }
        if(FAILED(hr))++failed;
        entries.push_back({name,kind,hit,hr,std::chrono::duration<double,std::milli>(Clock::now()-start).count()});
        if(FAILED(hr)){std::ostringstream s;s<<"Pipeline FAILED: "<<name<<" HRESULT=0x"<<std::hex<<uint32_t(hr);EngineLog::Write(s.str());}
        return hr;
    }
    HRESULT Graphics(const char* name,const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,REFIID iid,void** out){
        std::lock_guard lock(m_mutex);
        auto key=Key(name,desc->pRootSignature,{desc->VS,desc->PS,desc->HS,desc->DS,desc->GS});
        return Acquire(name,"graphics",key,iid,out,[&]{return m_library->LoadGraphicsPipeline(key.c_str(),desc,iid,out);},[&]{return m_device->CreateGraphicsPipelineState(desc,iid,out);});
    }
    HRESULT Compute(const char* name,const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,REFIID iid,void** out){
        std::lock_guard lock(m_mutex);auto key=Key(name,desc->pRootSignature,{desc->CS});
        return Acquire(name,"compute",key,iid,out,[&]{return m_library->LoadComputePipeline(key.c_str(),desc,iid,out);},[&]{return m_device->CreateComputePipelineState(desc,iid,out);});
    }
    HRESULT Mesh(const char* name,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,ID3D12RootSignature* root,std::initializer_list<D3D12_SHADER_BYTECODE> shaders,REFIID iid,void** out){
        std::lock_guard lock(m_mutex);auto key=Key(name,root,shaders);
        return Acquire(name,"mesh",key,iid,out,[&]{return m_library->LoadPipeline(key.c_str(),desc,iid,out);},[&]{return m_device->CreatePipelineState(desc,iid,out);});
    }
    void Flush(){
        if(!m_library||!m_dirty||m_lock==INVALID_HANDLE_VALUE)return;
        auto size=m_library->GetSerializedSize();if(!size||size>Limit)return;
        std::vector<char> data(size);if(FAILED(m_library->Serialize(data.data(),data.size())))return;
        auto temporary=m_file;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.write(data.data(),data.size());file.close();
        if(file&&MoveFileExW(temporary.c_str(),m_file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))m_dirty=false;
        else{std::error_code ec;std::filesystem::remove(temporary,ec);EngineLog::Write("Pipeline cache: could not persist library");}
    }
    void Shutdown(){
        std::lock_guard lock(m_mutex);Flush();
        auto expected=this;active.compare_exchange_strong(expected,nullptr);
        if(!entries.empty()){std::ostringstream s;s<<"Pipeline cache: hits="<<hits<<" misses="<<misses<<" failed="<<failed<<" storeFailed="<<storeFailed;EngineLog::Write(s.str());}
        m_library.Reset();m_seed.clear();m_roots.clear();
        if(m_lock!=INVALID_HANDLE_VALUE){CloseHandle(m_lock);m_lock=INVALID_HANDLE_VALUE;}
        entries.clear();m_dirty=false;
    }
    ~PipelineStateCache(){Shutdown();}
};
}
