#include <shellapi.h>
#pragma comment(lib,"shell32.lib")
static bool BackendArgument(const wchar_t* option){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);bool found=false;
    if(args){for(int i=1;i<count;++i)if(wcscmp(args[i],option)==0)found=true;LocalFree(args);}return found;
}
bool Renderer::InitializeVertexPipeline(){
    auto vs=CompileShaderDXC(L"Contents/shaders/raster_fallback.hlsl",L"RasterVS",L"vs_6_5");
    auto ps=CompileShaderDXC(L"Contents/shaders/shaders.hlsl",L"PSMain",L"ps_6_5");
    if(!vs||!ps)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=m_rootSignature.Get();
    d.VS={vs->GetBufferPointer(),vs->GetBufferSize()};d.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    d.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    d.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);d.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.SampleMask=UINT_MAX;d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.DSVFormat=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;d.NumRenderTargets=3;
    d.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;d.RTVFormats[1]=DXGI_FORMAT_R16G16_FLOAT;d.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(FAILED(m_pipelineCache.Graphics("vertex-main",&d,IID_PPV_ARGS(&m_pipelineState))))return false;
    d.NumRenderTargets=5;d.RTVFormats[3]=d.RTVFormats[4]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(FAILED(m_pipelineCache.Graphics("vertex-guides",&d,IID_PPV_ARGS(&m_rrPipelineState))))return false;
    d.NumRenderTargets=0;for(auto& format:d.RTVFormats)format=DXGI_FORMAT_UNKNOWN;d.PS={};
    d.RasterizerState.DepthBias=50;d.RasterizerState.SlopeScaledDepthBias=1;
    return SUCCEEDED(m_pipelineCache.Graphics("vertex-shadow",&d,IID_PPV_ARGS(&m_vsmShadowPipelineState)));
}
void Renderer::InitializeBackendQueries(){
    auto create=[&](D3D12_QUERY_HEAP_TYPE type,auto& query,auto& buffer,size_t size){
        D3D12_QUERY_HEAP_DESC d{};d.Type=type;d.Count=1;
        if(FAILED(m_device->CreateQueryHeap(&d,IID_PPV_ARGS(&query))))return;
        auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);auto desc=CD3DX12_RESOURCE_DESC::Buffer(size);
        m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer));
    };
    create(D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,m_computeQuery,m_computeStats,sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options{};
    if(m_meshShadersAvailable&&SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9,&options,sizeof(options)))&&options.MeshShaderPipelineStatsSupported)
        create(D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1,m_meshQuery,m_meshStats,sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1));
}
void Renderer::DrawGeometry(UINT groups,UINT indices){
    if(!indices)return;
    if(!m_meshShadersAvailable){m_commandList->DrawInstanced(indices,1,0,0);++m_vertexDraws;return;}
    if(!groups||groups>65535){if(groups>65535)LOG("Mesh dispatch rejected: group limit exceeded");return;}
    bool sample=m_meshDispatches==0&&m_meshQuery&&m_meshStats;
    if(sample)m_commandList->BeginQuery(m_meshQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,0);
    m_commandList->DispatchMesh(groups,1,1);++m_meshDispatches;
    if(sample){m_commandList->EndQuery(m_meshQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,0);m_commandList->ResolveQueryData(m_meshQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,0,1,m_meshStats.Get(),0);}
}
void Renderer::DispatchCompute(const char* pass,ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z){
    if(!x||!y||!z)return;
    if(x>65535||y>65535||z>65535){LOG("Compute dispatch rejected: "<<pass<<" exceeds group limits");return;}
    bool sample=m_computeDispatches==0&&m_computeQuery&&m_computeStats;
    if(sample)list->BeginQuery(m_computeQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
    list->Dispatch(x,y,z);++m_computeDispatches;++m_computePasses[pass];
    if(sample){list->EndQuery(m_computeQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);list->ResolveQueryData(m_computeQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,m_computeStats.Get(),0);}
}
void Renderer::ReportBackendStatistics(){
    LOG("Render backend: mesh="<<m_meshShadersAvailable<<" dxr="<<m_dxrAvailable<<" meshDispatches="<<m_meshDispatches<<" vertexDraws="<<m_vertexDraws<<" computeDispatches="<<m_computeDispatches);
    if(m_meshDispatches&&m_meshStats){
        D3D12_QUERY_DATA_PIPELINE_STATISTICS1* data=nullptr;D3D12_RANGE range{0,sizeof(*data)};
        if(SUCCEEDED(m_meshStats->Map(0,&range,reinterpret_cast<void**>(&data)))){
            LOG("Mesh GPU: amplification="<<data->ASInvocations<<" mesh="<<data->MSInvocations<<" primitives="<<data->MSPrimitives);
            D3D12_RANGE empty{};m_meshStats->Unmap(0,&empty);
        }
    }
    if(m_computeDispatches&&m_computeStats){
        D3D12_QUERY_DATA_PIPELINE_STATISTICS* data=nullptr;D3D12_RANGE range{0,sizeof(*data)};
        if(SUCCEEDED(m_computeStats->Map(0,&range,reinterpret_cast<void**>(&data)))){
            LOG("Compute GPU: invocations="<<data->CSInvocations);D3D12_RANGE empty{};m_computeStats->Unmap(0,&empty);
        }
    }
    m_meshStats.Reset();m_computeStats.Reset();m_meshQuery.Reset();m_computeQuery.Reset();
}
void Renderer::DrawPipelineDiagnostics(bool& visible){
#if defined(ENGINE_EDITOR)
    if(!visible)return;
    if(ImGui::Begin("Render Pipeline State",&visible)){
        ImGui::Text("Geometry: %s",m_meshShadersAvailable?"Amplification + Mesh Shaders":"Vertex shader fallback");
        ImGui::Text("DXR capability: %s",m_dxrAvailable?"available":"unavailable (raster rendering)");
        ImGui::Text("Mesh dispatches: %llu; vertex draws: %llu",m_meshDispatches,m_vertexDraws);
        ImGui::Text("Compute dispatches: %llu",m_computeDispatches);
        ImGui::Checkbox("Build hierarchical depth pyramid",&m_buildDepthPyramid);
        ImGui::TextDisabled("Optional depth preparation; mesh culling currently uses frustum planes.");
        ImGui::Text("PSO library: %s; hits %u; misses %u; failures %u",m_pipelineCache.Available()?"available":"direct creation",m_pipelineCache.hits,m_pipelineCache.misses,m_pipelineCache.failed);
        ImGui::TextWrapped("Cache: %s",EngineProject::Utf8(m_pipelineCache.Path()).c_str());
        if(ImGui::CollapsingHeader("Compute passes"))for(auto& [pass,count]:m_computePasses)ImGui::Text("%s: %llu",pass.c_str(),count);
        if(ImGui::BeginTable("pipelines",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)){
            ImGui::TableSetupColumn("Pipeline");ImGui::TableSetupColumn("Type");ImGui::TableSetupColumn("Result");ImGui::TableSetupColumn("Creation ms");ImGui::TableHeadersRow();
            for(auto& e:m_pipelineCache.entries){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(e.name.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(e.kind.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(FAILED(e.result)?"Failed":e.hit?"Cache hit":"Created");ImGui::TableNextColumn();ImGui::Text("%.3f",e.milliseconds);}
            ImGui::EndTable();
        }
    }ImGui::End();
#endif
}

void Renderer::BuildDepthPyramid(){
    if(!m_buildDepthPyramid||!m_hizTexture||!m_hizPipelineState||!m_depthStencilBuffer)return;
    const auto previous=m_depthStencilState;
    Transition(m_commandList.Get(),m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(m_commandList.Get(),m_hizTexture.Get(),m_hizTextureState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap* heaps[]={m_mainDescHeap.Get()};m_commandList->SetDescriptorHeaps(1,heaps);
    m_commandList->SetComputeRootSignature(m_hizRootSignature.Get());m_commandList->SetPipelineState(m_hizPipelineState.Get());
    auto desc=m_hizTexture->GetDesc();
    for(UINT mip=0;mip<desc.MipLevels;++mip){
        D3D12_RESOURCE_BARRIER barrier=CD3DX12_RESOURCE_BARRIER::Transition(m_hizTexture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,mip);
        m_commandList->ResourceBarrier(1,&barrier);
        auto input=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();input.ptr+=UINT64(mip?2100+mip-1:m_depthSrvIndex)*m_cbvSrvDescriptorSize;
        auto output=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();output.ptr+=UINT64(m_hizUavIndex+mip)*m_cbvSrvDescriptorSize;
        UINT size[]={(std::max)(1u,UINT(m_renderWidth)>>mip),(std::max)(1u,UINT(m_renderHeight)>>mip)};
        m_commandList->SetComputeRootDescriptorTable(0,input);m_commandList->SetComputeRootDescriptorTable(1,output);
        m_commandList->SetComputeRoot32BitConstants(2,2,size,0);
        DispatchCompute("HiZ",m_commandList.Get(),(size[0]+7)/8,(size[1]+7)/8,1);++m_depthPyramidDispatches;
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);m_commandList->ResourceBarrier(1,&barrier);
    }
    Transition(m_commandList.Get(),m_depthStencilBuffer.Get(),m_depthStencilState,previous);
}
