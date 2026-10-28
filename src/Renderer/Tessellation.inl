bool Renderer::InitializeTessellation(){
    m_tessellatedDraws=0;
    D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;query.Count=1;
    if(SUCCEEDED(m_device->CreateQueryHeap(&query,IID_PPV_ARGS(&m_tessellationQuery)))){
        auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        auto buffer=CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
        m_device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&m_tessellationStatistics));
    }
    auto vs=CompileShaderDXC(L"Contents/shaders/tessellation.hlsl",L"TessVS",L"vs_6_5");
    auto hs=CompileShaderDXC(L"Contents/shaders/tessellation.hlsl",L"TessHS",L"hs_6_5");
    auto ds=CompileShaderDXC(L"Contents/shaders/tessellation.hlsl",L"TessDS",L"ds_6_5");
    auto ps=CompileShaderDXC(L"Contents/shaders/shaders.hlsl",L"PSMain",L"ps_6_5");
    if(!vs||!hs||!ds||!ps)return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature=m_rootSignature.Get();
    d.VS={vs->GetBufferPointer(),vs->GetBufferSize()};d.HS={hs->GetBufferPointer(),hs->GetBufferSize()};
    d.DS={ds->GetBufferPointer(),ds->GetBufferSize()};d.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    d.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    d.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);d.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.SampleMask=UINT_MAX;d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    d.DSVFormat=DXGI_FORMAT_D32_FLOAT;d.SampleDesc.Count=1;d.NumRenderTargets=3;
    d.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;d.RTVFormats[1]=DXGI_FORMAT_R16G16_FLOAT;d.RTVFormats[2]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(FAILED(m_device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&m_tessellationPipeline))))return false;
    d.NumRenderTargets=5;d.RTVFormats[3]=d.RTVFormats[4]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(FAILED(m_device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&m_tessellationGBufferPipeline))))return false;
    d.NumRenderTargets=0;for(auto& format:d.RTVFormats)format=DXGI_FORMAT_UNKNOWN;d.PS={};
    d.RasterizerState.DepthBias=1000;d.RasterizerState.SlopeScaledDepthBias=1;
    return SUCCEEDED(m_device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&m_tessellationShadowPipeline)));
}
bool Renderer::DrawTessellated(const PBRMaterial& material,UINT count,bool shadow,bool gbuffer){
    if(!material.tessellation.enabled||m_dxrEnabled||!m_tessellationPipeline||!m_tessellationGBufferPipeline||!m_tessellationShadowPipeline||count<3)return false;
    auto t=material.tessellation;t.Normalize();
    float values[]={t.factor,t.smoothing,t.nearDistance,t.farDistance};
    m_commandList->SetGraphicsRoot32BitConstants(6,4,values,0);
    m_commandList->SetPipelineState(shadow?m_tessellationShadowPipeline.Get():(gbuffer?m_tessellationGBufferPipeline.Get():m_tessellationPipeline.Get()));
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    bool measure=m_tessellatedDraws==0&&m_tessellationQuery&&m_tessellationStatistics;
    if(measure)m_commandList->BeginQuery(m_tessellationQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
    m_commandList->DrawInstanced(count-count%3,1,0,0);
    if(measure){
        m_commandList->EndQuery(m_tessellationQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
        m_commandList->ResolveQueryData(m_tessellationQuery.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,m_tessellationStatistics.Get(),0);
    }
    ++m_tessellatedDraws;
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->SetPipelineState(shadow?m_vsmShadowPipelineState.Get():(gbuffer?m_rrPipelineState.Get():m_pipelineState.Get()));
    return true;
}
