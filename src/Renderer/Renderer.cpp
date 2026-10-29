#if defined(_WIN32)

#include "Renderer.h"
#include "AdvancedShaderDelivery.h"
#include "Debug/Profiler.h"
#include "VFX/ParticleSystem.h"
#include "DxrTransform.h"
#include "Common/SceneLighting.h"
#include "Core/DiagnosticPaths.h"
#include "Storage/ContentIO.h"
#include "Storage/ContentFile.h"
#include "Storage/PakValidation.h"
#include <sl_pcl.h>
#include <chrono>
static bool s_frameSynchronized=false;
#include "Camera.h"
#include <stdexcept>
#include <fstream>
#include <dxgidebug.h>
#include "TextureStreamer.h"
#include "Texture.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "tiny_obj_loader.h"
#include "cgltf.h"
#include "ufbx.h"
#include <filesystem>
#include "Application.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <vector>
#include <unordered_set>
#include <mutex>

#ifndef D3D12_TIGHT_ALIGNMENT_MIN_COMMITTED_RESOURCE_ALIGNMENT
#define D3D12_TIGHT_ALIGNMENT_MIN_COMMITTED_RESOURCE_ALIGNMENT 65536
#endif

#ifndef D3D12_TIGHT_ALIGNMENT_MIN_PLACED_RESOURCE_ALIGNMENT
#define D3D12_TIGHT_ALIGNMENT_MIN_PLACED_RESOURCE_ALIGNMENT 4096
#endif

#include "Crypto.h"
#include "PakCompression.h"
#include "Storage/DirectStorage/DirectStorageSystem.h"
#include "Upscaling/XeSS/XeSSSystem.h"
#include "Upscaling/FSR/FidelityFXFrameGeneration.h"
#include <directx/d3dx12.h>

std::vector<uint8_t> Renderer::LoadFromPak(const std::wstring& targetFile) {
    auto name=std::filesystem::path(targetFile).u16string();
    std::replace(name.begin(),name.end(),u'\\',u'/');
    auto pos=name.find(u"Content/");if(pos==std::u16string::npos)pos=name.find(u"shaders/");
    if(pos!=std::u16string::npos)name=name.substr(pos);
    return EngineStorage::ReadContentArchive(name);
}

namespace {
    struct SimpleDxcBlob final : public IDxcBlob
    {
        explicit SimpleDxcBlob(std::vector<uint8_t>&& bytes)
            : m_data(std::move(bytes))
        {
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
        {
            if (!ppvObject) return E_POINTER;
            *ppvObject = nullptr;

            if (riid == __uuidof(IUnknown) || riid == __uuidof(IDxcBlob))
            {
                *ppvObject = static_cast<IDxcBlob*>(this);
                AddRef();
                return S_OK;
            }

            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++m_refCount;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG rc = --m_refCount;
            if (rc == 0) delete this;
            return rc;
        }

        LPVOID STDMETHODCALLTYPE GetBufferPointer() override
        {
            return m_data.empty() ? nullptr : m_data.data();
        }

        SIZE_T STDMETHODCALLTYPE GetBufferSize() override
        {
            return m_data.size();
        }

    private:
        std::atomic<ULONG> m_refCount{ 1 };
        std::vector<uint8_t> m_data;
    };

    ComPtr<IDxcBlob> CompileShaderDXC(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& targetProfile) {
        std::filesystem::path p(filename);
        std::wstring pureFilename = p.filename().wstring();

        {
            std::wstring base = pureFilename;
            auto dotPos = base.rfind(L'.');
            if (dotPos != std::wstring::npos) base = base.substr(0, dotPos);
            std::wstring csoName = L"shaders/" + base + L"_" + entryPoint + L".cso";

            auto data = Renderer::LoadFromPak(csoName);
            if (!data.empty()) {
                LOG("PAK: Loaded pre-compiled " << std::string(csoName.begin(), csoName.end()));
                ComPtr<IDxcBlob> blob;
                blob.Attach(new SimpleDxcBlob(std::move(data)));
                return blob;
            }

            {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                std::wstring exe = exePath;
                size_t lastSlash = exe.find_last_of(L"\\/");
                std::wstring exeDir = (lastSlash != std::wstring::npos) ? exe.substr(0, lastSlash) : L"";

                std::wstring diskCso = exeDir + L"\\Contents\\shaders\\" + base + L"_" + entryPoint + L".cso";

                std::ifstream f(diskCso, std::ios::binary);
                if (f.is_open())
                {
                    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    if (!bytes.empty())
                    {
                        LOG("DISK: Loaded pre-compiled " << std::string(diskCso.begin(), diskCso.end()));
                        ComPtr<IDxcBlob> blob;
                        blob.Attach(new SimpleDxcBlob(std::move(bytes)));
                        return blob;
                    }
                }
            }
        }

        LOG("PAK: Runtime compile " << std::string(pureFilename.begin(), pureFilename.end()));
        
        auto hlslData = Renderer::LoadFromPak(L"shaders/" + pureFilename);
        if (hlslData.empty()) {
            LOG("PAK: Failed to find shader in pak: " << std::string(pureFilename.begin(), pureFilename.end()));
            return nullptr;
        }

        {
            char buf[8] = {};
            DWORD got = GetEnvironmentVariableA("ENABLE_DXC_RUNTIME_COMPILE", buf, (DWORD)sizeof(buf));
            bool allowRuntimeCompile = (got > 0) && (buf[0] == '1');
            if (!allowRuntimeCompile)
            {
                std::string shaderKey = std::string(pureFilename.begin(), pureFilename.end()) + "::" + std::string(entryPoint.begin(), entryPoint.end());
                LOG("DXC: Runtime compile blocked (ENABLE_DXC_RUNTIME_COMPILE!=1). Missing CSO for " << shaderKey);

                static std::mutex s_onceMutex;
                static std::unordered_set<std::string> s_shown;
                bool shouldShow = false;
                {
                    std::lock_guard<std::mutex> lock(s_onceMutex);
                    shouldShow = s_shown.insert(shaderKey).second;
                }
                if (shouldShow)
                {
                    std::string msg = "Runtime shader compilation is disabled to avoid DXC hang.\n\nMissing precompiled CSO for shader: ";
                    msg += shaderKey;
                    msg += "\n\nProvide shaders/<base>_<entry>.cso in pak or Contents/shaders, or set ENABLE_DXC_RUNTIME_COMPILE=1 to force runtime compile.";
                    MessageBoxA(nullptr, msg.c_str(), "Shader Compilation Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
                }
                return nullptr;
            }
        }

        LOG("DXC: Creating Compiler instance...");
        ComPtr<IDxcCompiler3> compiler;
        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)))) {
            LOG("DXC: Failed to create Compiler instance");
            return nullptr;
        }
        LOG("DXC: Creating Utils instance...");
        ComPtr<IDxcUtils> utils;
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))) {
            LOG("DXC: Failed to create Utils instance");
            return nullptr;
        }
        LOG("DXC: Creating Include Handler...");
        ComPtr<IDxcIncludeHandler> includeHandler;
        utils->CreateDefaultIncludeHandler(&includeHandler);

        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = hlslData.data();
        sourceBuffer.Size = hlslData.size();
        sourceBuffer.Encoding = 65001;

        std::vector<LPCWSTR> arguments = {
            pureFilename.c_str(),
            L"-E", entryPoint.c_str(),
            L"-T", targetProfile.c_str(),
            L"-Zi",
            L"-Qstrip_reflect",
            L"-Qstrip_debug"
        };

        LOG("DXC: Compiling stage " << std::string(entryPoint.begin(), entryPoint.end()) << "...");
        ComPtr<IDxcResult> result;
        HRESULT hrCompile = compiler->Compile(&sourceBuffer, arguments.data(), (UINT32)arguments.size(), includeHandler.Get(), IID_PPV_ARGS(&result));
        if (FAILED(hrCompile)) {
            LOG("DXC: Compile call failed with HRESULT: 0x" << std::hex << hrCompile);
            return nullptr;
        }
        LOG("DXC: Compile call finished.");

        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors != nullptr && errors->GetStringLength() != 0) {
            LOG("DXC Compile Error (" << std::string(pureFilename.begin(), pureFilename.end()) << "):\n" << errors->GetStringPointer());
        }

        HRESULT hrStatus;
        result->GetStatus(&hrStatus);
        if (FAILED(hrStatus)) {
            LOG("DXC: Result status FAILED");
            return nullptr;
        }

        ComPtr<IDxcBlob> shaderBlob;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
        LOG("DXC: Compiled " << std::string(pureFilename.begin(), pureFilename.end()) << " (" << std::string(entryPoint.begin(), entryPoint.end()) << ") successfully");
        return shaderBlob;
    }

    struct MeshShaderPipelineStream {
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE; ID3D12RootSignature* pRootSignature = nullptr; } RootSignature;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS; D3D12_SHADER_BYTECODE AS = {}; } AS;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS; D3D12_SHADER_BYTECODE MS = {}; } MS;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS; D3D12_SHADER_BYTECODE PS = {}; } PS;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER; D3D12_RASTERIZER_DESC RasterizerState = {}; } RasterizerState;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND; D3D12_BLEND_DESC BlendState = {}; } BlendState;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL; D3D12_DEPTH_STENCIL_DESC DepthStencilState = {}; } DepthStencilState;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK; UINT SampleMask = 0xFFFFFFFF; } SampleMask;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY; D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; } PrimitiveTopologyType;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS; D3D12_RT_FORMAT_ARRAY RTVFormats = {}; } RTVFormats;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT; DXGI_FORMAT DSVFormat = DXGI_FORMAT_UNKNOWN; } DSVFormat;
        struct alignas(void*) { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC; DXGI_SAMPLE_DESC SampleDesc = { 1, 0 }; } SampleDesc;
    };

}

void Renderer::Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* res, D3D12_RESOURCE_STATES& currentState, D3D12_RESOURCE_STATES newState) {
    if (!res || currentState == newState) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = currentState;
    b.Transition.StateAfter = newState;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
    currentState = newState;
}

ComPtr<ID3D12Resource> Renderer::CreateBuffer(uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, initState, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) return nullptr;
    return resource;
}

void Renderer::CreateBLAS() {
    if(!m_dxrDevice||!m_dxrCommandList)return;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (auto& model : m_models) {
        if (model.blas) continue;
        if (model.indexCount == 0) continue;

        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
        geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        const UINT totalVertexCount = (UINT)(m_vertexBuffer->GetDesc().Width / sizeof(Renderer::Vertex));
        const UINT localVertexCount = model.vertexCount > 0
            ? model.vertexCount
            : (model.baseVertexLocation >= 0 && (UINT)model.baseVertexLocation < totalVertexCount ? totalVertexCount - (UINT)model.baseVertexLocation : totalVertexCount);

        if (localVertexCount == 0) continue;

        geomDesc.Triangles.VertexBuffer.StartAddress = m_vertexBuffer->GetGPUVirtualAddress() + (model.baseVertexLocation * sizeof(Renderer::Vertex));
        geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Renderer::Vertex);
        geomDesc.Triangles.VertexCount = localVertexCount;
        geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geomDesc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress() + (model.startIndexLocation * sizeof(uint32_t));
        geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geomDesc.Triangles.IndexCount = model.indexCount;
        geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        BuildBLASForGeometry(geomDesc, model.blas, model.blasScratch);
    }

    for (auto& model : m_dynamicModels) {
        if (model.blas) continue;
        if (model.indexCount == 0 || !model.vertexBuffer || !model.indexBuffer) continue;

        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
        geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc.Triangles.VertexBuffer.StartAddress = model.vertexBuffer->GetGPUVirtualAddress();
        geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Renderer::Vertex);
        geomDesc.Triangles.VertexCount = model.vertexBuffer->GetDesc().Width / sizeof(Renderer::Vertex);
        geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geomDesc.Triangles.IndexBuffer = model.indexBuffer->GetGPUVirtualAddress();
        geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geomDesc.Triangles.IndexCount = model.indexCount;
        geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        BuildBLASForGeometry(geomDesc, model.blas, model.blasScratch);
    }
}

void Renderer::BuildBLASForGeometry(const D3D12_RAYTRACING_GEOMETRY_DESC& geomDesc, ComPtr<ID3D12Resource>& blas, ComPtr<ID3D12Resource>& scratch) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geomDesc;
    inputs.NumDescs = 1;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (blas) m_retiredResources.push_back(blas);
    if (scratch) m_retiredResources.push_back(scratch);

    blas = CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeap);
    scratch = CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeap);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();

    m_dxrCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = blas.Get();
    m_commandList->ResourceBarrier(1, &uavBarrier);
}

static uint64_t GIHistoryHash(const void* data, size_t size, uint64_t hash=14695981039346656037ULL) {
    const auto* bytes=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<size;++i){hash^=bytes[i];hash*=1099511628211ULL;}
    return hash;
}

void Renderer::CreateTLAS() {
    if (!m_dxrDevice) return;

    // Ensure all models have BLAS built before generating TLAS instances
    CreateBLAS();

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    
    for (size_t i = 0; i < m_models.size(); ++i) {
        if (!m_models[i].blas || !m_models[i].visible) continue;
        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        XMMATRIX mScale = XMMatrixScaling(m_models[i].scale.x, m_models[i].scale.y, m_models[i].scale.z);
        XMMATRIX mRot = XMMatrixRotationRollPitchYaw(XMConvertToRadians(m_models[i].rotation.x), 
                                                     XMConvertToRadians(m_models[i].rotation.y), 
                                                     XMConvertToRadians(m_models[i].rotation.z));
        XMMATRIX mTrans = XMMatrixTranslation(m_models[i].position.x, m_models[i].position.y, m_models[i].position.z);
        XMMATRIX transform = mScale * mRot * mTrans;
        XMStoreFloat4x4(&m_models[i].worldMatrix, transform);
        EngineRender::StoreDxrTransform(desc.Transform, transform);
        desc.InstanceID = (UINT)i;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_models[i].blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    // Dynamic geometry has dedicated SRV slots for the hit shader.
    if (m_dynamicModels.size() > 2048) {
        static bool warned=false;
        if (!warned) { LOG("DXR: dynamic geometry exceeds the 2048-object descriptor limit"); warned=true; }
    }
    for (size_t i = 0; i < m_dynamicModels.size() && i < 2048; ++i) {
        if (!m_dynamicModels[i].blas || !m_dynamicModels[i].visible) continue;
        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        XMMATRIX mScale = XMMatrixScaling(m_dynamicModels[i].scale.x, m_dynamicModels[i].scale.y, m_dynamicModels[i].scale.z);
        XMMATRIX mRot = XMMatrixRotationRollPitchYaw(XMConvertToRadians(m_dynamicModels[i].rotation.x), 
                                                     XMConvertToRadians(m_dynamicModels[i].rotation.y), 
                                                     XMConvertToRadians(m_dynamicModels[i].rotation.z));
        XMMATRIX mTrans = XMMatrixTranslation(m_dynamicModels[i].position.x, m_dynamicModels[i].position.y, m_dynamicModels[i].position.z);
        XMMATRIX transform = mScale * mRot * mTrans;
        XMStoreFloat4x4(&m_dynamicModels[i].worldMatrix, transform);
        EngineRender::StoreDxrTransform(desc.Transform, transform);
        desc.InstanceID = (UINT)(m_models.size() + i);
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = m_dynamicModels[i].blas->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    }

    if (instanceDescs.empty()) return;

    UINT64 bufferSize = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    const auto signature=GIHistoryHash(instanceDescs.data(),static_cast<size_t>(bufferSize));
    if(signature!=m_giInstanceSignature){m_giHistoryValid=false;m_giInstanceSignature=signature;}
    if (!m_instanceDescs || m_instanceDescs->GetDesc().Width < bufferSize) {
        if (m_instanceDescs) m_retiredResources.push_back(m_instanceDescs);
        m_instanceDescs = CreateBuffer(bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeap);
    }

    void* pData;
    if (SUCCEEDED(m_instanceDescs->Map(0, nullptr, &pData))) {
        memcpy(pData, instanceDescs.data(), bufferSize);
        m_instanceDescs->Unmap(0, nullptr);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = m_instanceDescs->GetGPUVirtualAddress();
    inputs.NumDescs = (UINT)instanceDescs.size();
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    if (!m_tlas || m_tlas->GetDesc().Width < info.ResultDataMaxSizeInBytes) {
        if (m_tlas) m_retiredResources.push_back(m_tlas);
        m_tlas = CreateBuffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, defaultHeap);
        m_tlas->SetName(L"TLAS");
    }
    if (!m_tlasScratch || m_tlasScratch->GetDesc().Width < info.ScratchDataSizeInBytes) {
        if (m_tlasScratch) m_retiredResources.push_back(m_tlasScratch);
        m_tlasScratch = CreateBuffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, defaultHeap);
        m_tlasScratch->SetName(L"TLASScratch");
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();

    m_dxrCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_tlas.Get();
    m_commandList->ResourceBarrier(1, &uavBarrier);
}

void Renderer::CreateRayTracingOutputResource() {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Width = m_renderWidth;
    desc.Height = m_renderHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_rayTracingOutput)))) {
        return;
    }
    m_rayTracingOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_rayTracingOutput->SetName(L"RayTracingOutput");

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += m_rayTracingOutputRtvIndex * m_rtvDescriptorSize;
    m_device->CreateRenderTargetView(m_rayTracingOutput.Get(), nullptr, rtvHandle);
    
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    
    D3D12_CPU_DESCRIPTOR_HANDLE uavHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    uavHeapHandle.ptr += m_rayTracingOutputUavIndex * m_cbvSrvDescriptorSize;
    m_device->CreateUnorderedAccessView(m_rayTracingOutput.Get(), nullptr, &uavDesc, uavHeapHandle);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    srvHeapHandle.ptr += m_rayTracingOutputSrvIndex * m_cbvSrvDescriptorSize;
    m_device->CreateShaderResourceView(m_rayTracingOutput.Get(), &srvDesc, srvHeapHandle);
    m_giHistory.Reset();m_giHistoryValid=false;
    auto historyDesc=desc;historyDesc.Flags=D3D12_RESOURCE_FLAG_NONE;
    m_giHistoryState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if(SUCCEEDED(m_device->CreateCommittedResource(&m_heapPropsDefault,D3D12_HEAP_FLAG_NONE,&historyDesc,m_giHistoryState,nullptr,IID_PPV_ARGS(&m_giHistory)))){
        auto historySrv=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();historySrv.ptr+=1513*m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_giHistory.Get(),&srvDesc,historySrv);
    }else{LOG("GI history allocation failed");}
    m_rrHitDistance.Reset();desc.Format=DXGI_FORMAT_R32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;m_rrHitDistanceState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if(SUCCEEDED(m_device->CreateCommittedResource(&m_heapPropsDefault,D3D12_HEAP_FLAG_NONE,&desc,m_rrHitDistanceState,nullptr,IID_PPV_ARGS(&m_rrHitDistance)))){
        uavDesc.Format=DXGI_FORMAT_R32_FLOAT;uavHeapHandle=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();uavHeapHandle.ptr+=2025*m_cbvSrvDescriptorSize;m_device->CreateUnorderedAccessView(m_rrHitDistance.Get(),nullptr,&uavDesc,uavHeapHandle);
        srvDesc.Format=DXGI_FORMAT_R32_FLOAT;srvHeapHandle=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();srvHeapHandle.ptr+=1512*m_cbvSrvDescriptorSize;m_device->CreateShaderResourceView(m_rrHitDistance.Get(),&srvDesc,srvHeapHandle);
    }
}

void Renderer::CreateRayTracingPipeline() {
    if(!m_dxrDevice)return;
    D3D12_ROOT_PARAMETER1 rootParams[14]{};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[0].Descriptor.ShaderRegister = 0;
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 0;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[3].Descriptor.ShaderRegister = 1;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[4].Descriptor.ShaderRegister = 2;
    rootParams[4].Descriptor.RegisterSpace = 0;
    rootParams[4].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[5].Descriptor.ShaderRegister = 3;
    rootParams[5].Descriptor.RegisterSpace = 0;
    rootParams[5].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
    rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 texTableRange = {};
    texTableRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texTableRange.NumDescriptors = (UINT)-1; // Unbounded array
    texTableRange.BaseShaderRegister = 4;
    texTableRange.RegisterSpace = 0;
    texTableRange.OffsetInDescriptorsFromTableStart = 0;
    texTableRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

    rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[6].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[6].DescriptorTable.pDescriptorRanges = &texTableRange;
    rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 samplerRange = {};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = 1;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;
    samplerRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

    rootParams[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[7].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[7].DescriptorTable.pDescriptorRanges = &samplerRange;
    rootParams[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 astraRvtRange = {};
    astraRvtRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    astraRvtRange.NumDescriptors = 3; // Albedo, Normal, Irradiance
    astraRvtRange.BaseShaderRegister = 10; // t10, t11, t12
    astraRvtRange.RegisterSpace = 1; // UPDATE: Matches space1 in raytracing.hlsl
    astraRvtRange.OffsetInDescriptorsFromTableStart = 0; // Wait, actually it binds after something else?
    // Wait, in InitDXR we bind it by doing what? We'll see...
    astraRvtRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

    rootParams[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[8].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[8].DescriptorTable.pDescriptorRanges = &astraRvtRange;
    rootParams[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE1 hitDistanceRange=uavRange;hitDistanceRange.BaseShaderRegister=1;
    rootParams[9].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParams[9].DescriptorTable={1,&hitDistanceRange};rootParams[9].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 diffuseGiRange=uavRange;diffuseGiRange.BaseShaderRegister=2;
    rootParams[10].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParams[10].DescriptorTable={1,&diffuseGiRange};rootParams[10].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE1 historyRange={};historyRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;historyRange.NumDescriptors=1;historyRange.BaseShaderRegister=13;historyRange.RegisterSpace=1;
    rootParams[11].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParams[11].DescriptorTable={1,&historyRange};rootParams[11].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE1 dynamicVertexRange={};dynamicVertexRange.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;dynamicVertexRange.NumDescriptors=2048;dynamicVertexRange.BaseShaderRegister=0;dynamicVertexRange.RegisterSpace=2;dynamicVertexRange.Flags=D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    D3D12_DESCRIPTOR_RANGE1 dynamicIndexRange=dynamicVertexRange;dynamicIndexRange.RegisterSpace=3;
    rootParams[12].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParams[12].DescriptorTable={1,&dynamicVertexRange};rootParams[12].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    rootParams[13].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rootParams[13].DescriptorTable={1,&dynamicIndexRange};rootParams[13].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version = m_highestRootSignatureVersion;
    if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
        rsDesc.Desc_1_1.NumParameters = 14;
        rsDesc.Desc_1_1.pParameters = rootParams;
        rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    } else {
        static D3D12_ROOT_PARAMETER rootParamsV10[14];
        static D3D12_DESCRIPTOR_RANGE rangesV10[14][1];
        for(int i=0; i<14; ++i) {
            rootParamsV10[i].ParameterType = rootParams[i].ParameterType;
            rootParamsV10[i].ShaderVisibility = rootParams[i].ShaderVisibility;
            if(rootParams[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
               rootParams[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
               rootParams[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) {
                rootParamsV10[i].Descriptor.ShaderRegister = rootParams[i].Descriptor.ShaderRegister;
                rootParamsV10[i].Descriptor.RegisterSpace = rootParams[i].Descriptor.RegisterSpace;
            } else {
                rootParamsV10[i].DescriptorTable.NumDescriptorRanges = rootParams[i].DescriptorTable.NumDescriptorRanges;
                rangesV10[i][0].RangeType = rootParams[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                rangesV10[i][0].NumDescriptors = rootParams[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                rangesV10[i][0].BaseShaderRegister = rootParams[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                rangesV10[i][0].RegisterSpace = rootParams[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                rangesV10[i][0].OffsetInDescriptorsFromTableStart = rootParams[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                rootParamsV10[i].DescriptorTable.pDescriptorRanges = rangesV10[i];
            }
        }
        rsDesc.Desc_1_0.NumParameters = 14;
        rsDesc.Desc_1_0.pParameters = rootParamsV10;
        rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    }

    if (FAILED(CreateRootSignature(&rsDesc, &m_dxrGlobalRootSignature))) {
        LOG("Failed to create DXR Global Root Signature");
    }

    ComPtr<IDxcBlob> raytracingShader = CompileShaderDXC(L"Contents/shaders/raytracing.hlsl", L"", L"lib_6_3");
    if (!raytracingShader) return;

    D3D12_STATE_SUBOBJECT subobjects[10];
    UINT subobjectCount = 0;

    D3D12_STATE_SUBOBJECT rootSigSub = {};
    rootSigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    ID3D12RootSignature* pRootSig = m_dxrGlobalRootSignature.Get();
    rootSigSub.pDesc = &pRootSig;
    subobjects[subobjectCount++] = rootSigSub;

    D3D12_EXPORT_DESC exports[4] = {};
    exports[0].Name = L"RayGen";
    exports[1].Name = L"Miss";
    exports[2].Name = L"ShadowMiss";
    exports[3].Name = L"ClosestHit";

    D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
    dxilLibDesc.DXILLibrary.pShaderBytecode = raytracingShader->GetBufferPointer();
    dxilLibDesc.DXILLibrary.BytecodeLength = raytracingShader->GetBufferSize();
    dxilLibDesc.NumExports = 4;
    dxilLibDesc.pExports = exports;

    D3D12_STATE_SUBOBJECT dxilLibSub = {};
    dxilLibSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    dxilLibSub.pDesc = &dxilLibDesc;
    subobjects[subobjectCount++] = dxilLibSub;

    D3D12_HIT_GROUP_DESC hitGroupDesc = {};
    hitGroupDesc.HitGroupExport = L"MyHitGroup";
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;

    D3D12_STATE_SUBOBJECT hitGroupSub = {};
    hitGroupSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    hitGroupSub.pDesc = &hitGroupDesc;
    subobjects[subobjectCount++] = hitGroupSub;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = 128;
    shaderConfig.MaxAttributeSizeInBytes = 8;

    D3D12_STATE_SUBOBJECT shaderConfigSub = {};
    shaderConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    shaderConfigSub.pDesc = &shaderConfig;
    subobjects[subobjectCount++] = shaderConfigSub;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 4;

    D3D12_STATE_SUBOBJECT pipelineConfigSub = {};
    pipelineConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    pipelineConfigSub.pDesc = &pipelineConfig;
    subobjects[subobjectCount++] = pipelineConfigSub;

    D3D12_STATE_OBJECT_DESC pipeDesc = {};
    pipeDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipeDesc.NumSubobjects = subobjectCount;
    pipeDesc.pSubobjects = subobjects;

    HRESULT hr = m_dxrDevice->CreateStateObject(&pipeDesc, IID_PPV_ARGS(&m_dxrStateObject));
    if (FAILED(hr)) {
        LOG("ERROR: RTPSO creation failed: 0x"<<std::hex<<hr);
        std::ofstream logFile(EngineDiagnostics::Directory()/"renderer_error.log", std::ios::app);
        logFile << "RTPSO Creation Failed: " << std::hex << hr << "\n";
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            UINT64 messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < messageCount; ++i) {
                SIZE_T messageLength = 0;
                infoQueue->GetMessage(i, nullptr, &messageLength);
                D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
                if (pMessage) {
                    infoQueue->GetMessage(i, pMessage, &messageLength);
                    logFile << "D3D12 Error: " << pMessage->pDescription << "\n";
                    free(pMessage);
                }
            }
        }
        return;
    }
}

void Renderer::CreateShaderTable() {
    if (!m_dxrStateObject) return;

    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    if (FAILED(m_dxrStateObject.As(&stateObjectProperties))) return;

    void* rayGenShaderIdentifier = stateObjectProperties->GetShaderIdentifier(L"RayGen");
    void* missShaderIdentifier = stateObjectProperties->GetShaderIdentifier(L"Miss");
    void* shadowMissShaderIdentifier = stateObjectProperties->GetShaderIdentifier(L"ShadowMiss");
    void* hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(L"MyHitGroup");

    if (!rayGenShaderIdentifier || !missShaderIdentifier || !shadowMissShaderIdentifier || !hitGroupShaderIdentifier) {
        LOG("Shader Identifier is NULL!");
        return;
    }
        UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    UINT shaderRecordSize = ALIGN_UP(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    
    UINT rayGenSectionSize = ALIGN_UP(shaderRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    UINT missSectionSize = ALIGN_UP(shaderRecordSize * 2, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    UINT hitGroupSectionSize = ALIGN_UP(shaderRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    
    UINT totalShaderTableSize = rayGenSectionSize + missSectionSize + hitGroupSectionSize;

        D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalShaderTableSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(&m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_shaderTable)))) {
        return;
    }

        uint8_t* pData;
    m_shaderTable->Map(0, nullptr, (void**)&pData);
    
    memcpy(pData, rayGenShaderIdentifier, shaderIdentifierSize);
    pData += rayGenSectionSize;
    
    memcpy(pData, missShaderIdentifier, shaderIdentifierSize);
    memcpy(pData + shaderRecordSize, shadowMissShaderIdentifier, shaderIdentifierSize);
    pData += missSectionSize;
    
    memcpy(pData, hitGroupShaderIdentifier, shaderIdentifierSize);
    
        m_shaderTable->Unmap(0, nullptr);
}

void Renderer::RebuildRaytracingScene() {
    if (!m_dxrDevice) return;

    for (auto& model : m_models) {
        if (model.blas) m_retiredResources.push_back(model.blas);
        if (model.blasScratch) m_retiredResources.push_back(model.blasScratch);
        model.blas.Reset();
        model.blasScratch.Reset();
    }
    for (auto& model : m_dynamicModels) {
        if (model.blas) m_retiredResources.push_back(model.blas);
        if (model.blasScratch) m_retiredResources.push_back(model.blasScratch);
        model.blas.Reset();
        model.blasScratch.Reset();
    }

    CreateBLAS();
    CreateTLAS();
    m_rtSceneDirty = false;
}

void Renderer::DoRayTracing() {
    if (!m_dxrDevice || !m_dxrStateObject || !m_shaderTable || !m_dxrEnabled) return;

    if (m_rayTracingOutput) {
        Transition(m_commandList.Get(), m_rayTracingOutput.Get(), m_rayTracingOutputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_rayTracingOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ID3D12DescriptorHeap* heaps[] = { m_mainDescHeap.Get(), m_samplerHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

    if (!m_tlas) {
        LOG("DoRayTracing: skipped - TLAS is null");
        return;
    }

    m_commandList->SetComputeRootSignature(m_dxrGlobalRootSignature.Get());
    auto dynamicVertices=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();dynamicVertices.ptr+=UINT64(4096)*m_cbvSrvDescriptorSize;
    auto dynamicIndices=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();dynamicIndices.ptr+=UINT64(6144)*m_cbvSrvDescriptorSize;
    m_commandList->SetComputeRootDescriptorTable(12,dynamicVertices);
    m_commandList->SetComputeRootDescriptorTable(13,dynamicIndices);

    m_commandList->SetComputeRootShaderResourceView(0, m_tlas->GetGPUVirtualAddress());
    
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    uavHandle.ptr += m_rayTracingOutputUavIndex * m_cbvSrvDescriptorSize;
    m_commandList->SetComputeRootDescriptorTable(1, uavHandle);
    Transition(m_commandList.Get(),m_rrHitDistance.Get(),m_rrHitDistanceState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto hitDistanceHandle=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();hitDistanceHandle.ptr+=2025*m_cbvSrvDescriptorSize;m_commandList->SetComputeRootDescriptorTable(9,hitDistanceHandle);
    Transition(m_commandList.Get(),m_ssgiTexture.Get(),m_ssgiState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto diffuseGiHandle=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();diffuseGiHandle.ptr+=m_ssgiUavIndex*m_cbvSrvDescriptorSize;m_commandList->SetComputeRootDescriptorTable(10,diffuseGiHandle);

    if(m_giHistory)Transition(m_commandList.Get(),m_giHistory.Get(),m_giHistoryState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto historyHandle=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();historyHandle.ptr+=UINT64(m_giHistory?1513:m_nullSrvIndex)*m_cbvSrvDescriptorSize;
    m_commandList->SetComputeRootDescriptorTable(11,historyHandle);
    XMMATRIX view = m_camera->GetViewMatrix();
    float fov = XMConvertToRadians(m_fov);
    float aspect = static_cast<float>(m_renderWidth) / m_renderHeight;
    XMMATRIX cleanProj = m_camera->GetCleanProjectionMatrix(fov, aspect, 0.1f, 2000.0f);

    XMMATRIX invView = XMMatrixInverse(nullptr, view);

    XMFLOAT3 camPos;
    XMStoreFloat3(&camPos, invView.r[3]);

    XMFLOAT3 camRight;
    XMStoreFloat3(&camRight, invView.r[0]);

    XMFLOAT3 camUp;
    XMStoreFloat3(&camUp, invView.r[1]);

    XMFLOAT3 camForward;
    XMStoreFloat3(&camForward, invView.r[2]);

    float tanHalfFovX = 1.0f / XMVectorGetX(cleanProj.r[0]);
    float tanHalfFovY = 1.0f / XMVectorGetY(cleanProj.r[1]);

    m_rtConstants.cameraPos = XMFLOAT4(camPos.x, camPos.y, camPos.z, 1.0f);
    m_rtConstants.cameraForward = XMFLOAT4(camForward.x, camForward.y, camForward.z, 0.0f);
    m_rtConstants.cameraRight = XMFLOAT4(camRight.x * tanHalfFovX, camRight.y * tanHalfFovX, camRight.z * tanHalfFovX, 0.0f);
    m_rtConstants.cameraUp = XMFLOAT4(camUp.x * tanHalfFovY, camUp.y * tanHalfFovY, camUp.z * tanHalfFovY, 0.0f);
    m_rtConstants.cameraForward.x-=m_camera->GetJitterX()*m_rtConstants.cameraRight.x+m_camera->GetJitterY()*m_rtConstants.cameraUp.x;
    m_rtConstants.cameraForward.y-=m_camera->GetJitterX()*m_rtConstants.cameraRight.y+m_camera->GetJitterY()*m_rtConstants.cameraUp.y;
    m_rtConstants.cameraForward.z-=m_camera->GetJitterX()*m_rtConstants.cameraRight.z+m_camera->GetJitterY()*m_rtConstants.cameraUp.z;
    m_rtConstants.lightDir = XMFLOAT4(m_constantBufferData.lightDir.x, m_constantBufferData.lightDir.y, m_constantBufferData.lightDir.z, 0.0f);
    m_rtConstants.lightColor = XMFLOAT4(m_constantBufferData.lightColor.x, m_constantBufferData.lightColor.y, m_constantBufferData.lightColor.z, 1.0f);
    m_rtConstants.shadowsEnabled = m_rtShadowsEnabled ? 1 : 0;
    m_rtConstants.reflectionsEnabled = m_rtReflectionsEnabled ? 1 : 0;
    m_rtConstants.pathTracingEnabled = m_pathTracingEnabled ? 1 : 0;
    m_rtConstants.maxBounces = m_ptMaxBounces;
    m_rtConstants.frameIndex = m_frameCounter;
    m_rtConstants.samplesPerPixel = m_ptSamplesPerPixel;
    m_rtConstants.cloudDensity = m_cloudDensity;
    m_rtConstants.cloudTime = m_cloudTime;
    m_rtConstants.rainWetness = m_constantBufferData.rainWetness;
    m_rtConstants.rainPuddles = m_constantBufferData.rainPuddles;
    m_rtConstants.pad1=m_ssgiEnabled?m_ssgiIntensity:0.f;
    m_rtConstants.numPointLights=m_constantBufferData.numPointLights;
    m_rtConstants.numSpotLights=m_constantBufferData.numSpotLights;
    std::copy(std::begin(m_constantBufferData.pointLights),std::end(m_constantBufferData.pointLights),std::begin(m_rtConstants.pointLights));
    std::copy(std::begin(m_constantBufferData.spotLights),std::end(m_constantBufferData.spotLights),std::begin(m_rtConstants.spotLights));

    if (m_pRtCbvDataBegin) {
        memcpy(m_pRtCbvDataBegin, &m_rtConstants, sizeof(m_rtConstants));
    }
    m_commandList->SetComputeRootConstantBufferView(2, m_rtConstantBuffer->GetGPUVirtualAddress());

    m_commandList->SetComputeRootShaderResourceView(3, m_vertexBuffer->GetGPUVirtualAddress());
    m_commandList->SetComputeRootShaderResourceView(4, m_indexBuffer->GetGPUVirtualAddress());

    m_rtMaterialData.clear();
    auto getMatIndex = [&](const std::shared_ptr<Texture>& tex, int fallbackIdx) -> UINT {
        if (tex && tex->GetState() == TextureState::Ready) return tex->GetSrvIndex();
        if (fallbackIdx >= 0) return (UINT)(fallbackIdx + 1);
        return 0;
    };

    for (auto& model : m_models) {
        MaterialData md = {};
        md.pad.x = static_cast<float>(m_constantBufferData.padIndices);
        md.albedoIndex = getMatIndex(model.material.albedoMap ? model.material.albedoMap : model.albedoMap, model.albedoIndex);
        if (md.albedoIndex == 0 && model.texture && model.texture->GetState() == TextureState::Ready) {
            md.albedoIndex = model.texture->GetSrvIndex();
        }
        md.normalIndex = getMatIndex(model.material.normalMap ? model.material.normalMap : model.normalMap, model.normalIndex);
        md.rmaIndex = getMatIndex(model.material.rmaMap ? model.material.rmaMap : model.rmaMap, model.rmaIndex);
        md.aoIndex = getMatIndex(model.material.aoMap, model.aoIndex);
        md.emissiveIndex = getMatIndex(model.material.emissiveMap, model.emissiveIndex);
        md.indexFormat = 1; // R32_UINT index buffer
        md.startIndexLocation = model.startIndexLocation;
        md.baseVertexLocation = model.baseVertexLocation;
        md.albedo = model.material.albedo;
        md.roughness = model.material.roughness;
        md.metallic = model.material.metallic;
        md.reflectance = model.material.reflectance;
        md.emission = model.material.emission;
        md.ior = model.material.ior;
        md.uvTiling = model.material.uvTiling;
        m_rtMaterialData.push_back(md);
    }
    for (size_t dynamicIndex=0; dynamicIndex<m_dynamicModels.size(); ++dynamicIndex) {
        auto& model=m_dynamicModels[dynamicIndex];
        MaterialData md = {};
        md.pad.x = static_cast<float>(m_constantBufferData.padIndices);
        if(dynamicIndex<2048 && model.vertexBuffer && model.indexBuffer){
            md.pad.y=static_cast<float>(dynamicIndex+1);
            auto cpu=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=UINT64(4096+dynamicIndex)*m_cbvSrvDescriptorSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC vertices={};vertices.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;vertices.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;vertices.Format=DXGI_FORMAT_UNKNOWN;vertices.Buffer.NumElements=static_cast<UINT>(model.vertexBuffer->GetDesc().Width/sizeof(Renderer::Vertex));vertices.Buffer.StructureByteStride=sizeof(Renderer::Vertex);
            m_device->CreateShaderResourceView(model.vertexBuffer.Get(),&vertices,cpu);
            cpu=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=UINT64(6144+dynamicIndex)*m_cbvSrvDescriptorSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC indices={};indices.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;indices.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;indices.Format=DXGI_FORMAT_R32_TYPELESS;indices.Buffer.NumElements=static_cast<UINT>(model.indexBuffer->GetDesc().Width/4);indices.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
            m_device->CreateShaderResourceView(model.indexBuffer.Get(),&indices,cpu);
        }
        md.albedoIndex = getMatIndex(model.material.albedoMap ? model.material.albedoMap : model.albedoMap, model.albedoIndex);
        if (md.albedoIndex == 0 && model.texture && model.texture->GetState() == TextureState::Ready) {
            md.albedoIndex = model.texture->GetSrvIndex();
        }
        md.normalIndex = getMatIndex(model.material.normalMap ? model.material.normalMap : model.normalMap, model.normalIndex);
        md.rmaIndex = getMatIndex(model.material.rmaMap ? model.material.rmaMap : model.rmaMap, model.rmaIndex);
        md.aoIndex = getMatIndex(model.material.aoMap, model.aoIndex);
        md.emissiveIndex = getMatIndex(model.material.emissiveMap, model.emissiveIndex);
        md.indexFormat = 1;
        md.startIndexLocation = 0;
        md.baseVertexLocation = 0;
        md.albedo = model.material.albedo;
        md.roughness = model.material.roughness;
        md.metallic = model.material.metallic;
        md.reflectance = model.material.reflectance;
        md.emission = model.material.emission;
        md.ior = model.material.ior;
        md.uvTiling = model.material.uvTiling;
        m_rtMaterialData.push_back(md);
    }

    if (!m_rtMaterialData.empty()) {
        UINT64 bufferSize = m_rtMaterialData.size() * sizeof(MaterialData);
        if (!m_rtMaterialBuffer || m_rtMaterialBuffer->GetDesc().Width < bufferSize) {
            D3D12_HEAP_PROPERTIES uploadHeap = {};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            m_rtMaterialBuffer = CreateBuffer(bufferSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeap);
            m_rtMaterialBuffer->Map(0, nullptr, &m_pRtMaterialDataBegin);
        }
        if (m_pRtMaterialDataBegin) {
            memcpy(m_pRtMaterialDataBegin, m_rtMaterialData.data(), bufferSize);
        }
    }

    if (m_rtMaterialBuffer) {
        m_commandList->SetComputeRootShaderResourceView(5, m_rtMaterialBuffer->GetGPUVirtualAddress());
    } else {
        m_commandList->SetComputeRootShaderResourceView(5, m_vertexBuffer->GetGPUVirtualAddress());
    }

    m_commandList->SetComputeRootDescriptorTable(6, m_mainDescHeap->GetGPUDescriptorHandleForHeapStart());

    if (m_samplerHeap) m_commandList->SetComputeRootDescriptorTable(7, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
    
    D3D12_GPU_DESCRIPTOR_HANDLE astraRvtHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    astraRvtHandle.ptr += m_rvtAlbedoAtlasSrvIndex * m_cbvSrvDescriptorSize;
    m_commandList->SetComputeRootDescriptorTable(8, astraRvtHandle);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.Width = m_renderWidth;
    dispatchDesc.Height = m_renderHeight;
    dispatchDesc.Depth = 1;

    UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    UINT shaderRecordSize = ALIGN_UP(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    UINT rayGenSectionSize = ALIGN_UP(shaderRecordSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    UINT missSectionSize = ALIGN_UP(shaderRecordSize * 2, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
    
    D3D12_GPU_VIRTUAL_ADDRESS shaderTableAddress = m_shaderTable->GetGPUVirtualAddress();

    dispatchDesc.RayGenerationShaderRecord.StartAddress = shaderTableAddress;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = shaderRecordSize;

    dispatchDesc.MissShaderTable.StartAddress = shaderTableAddress + rayGenSectionSize;
    dispatchDesc.MissShaderTable.SizeInBytes = shaderRecordSize * 2;
    dispatchDesc.MissShaderTable.StrideInBytes = shaderRecordSize;

    dispatchDesc.HitGroupTable.StartAddress = shaderTableAddress + rayGenSectionSize + missSectionSize;
    dispatchDesc.HitGroupTable.SizeInBytes = shaderRecordSize;
    dispatchDesc.HitGroupTable.StrideInBytes = shaderRecordSize;

    uint64_t lightingSignature=GIHistoryHash(m_rtMaterialData.data(),m_rtMaterialData.size()*sizeof(MaterialData));
    lightingSignature=GIHistoryHash(&m_rtConstants.lightDir,2*sizeof(XMFLOAT4),lightingSignature);
    lightingSignature=GIHistoryHash(&m_rtConstants.shadowsEnabled,2*sizeof(int),lightingSignature);
    lightingSignature=GIHistoryHash(&m_rtConstants.cloudDensity,sizeof(float),lightingSignature);
    lightingSignature=GIHistoryHash(&m_rtConstants.rainWetness,3*sizeof(float),lightingSignature);
    lightingSignature=GIHistoryHash(m_rtConstants.pointLights,sizeof(m_rtConstants.pointLights),lightingSignature);
    lightingSignature=GIHistoryHash(m_rtConstants.spotLights,sizeof(m_rtConstants.spotLights),lightingSignature);
    if(lightingSignature!=m_giLightingSignature || m_frameCounter!=m_giHistoryLastFrame+1)m_giHistoryValid=false;
    m_giLightingSignature=lightingSignature;
    m_rtConstants.pad2=(m_giHistoryValid&&m_giHistory)?1.f:0.f;
    m_rtConstants.giPreviousViewProjection=m_giPreviousViewProjection;
    m_rtConstants.giPreviousCamera=m_giPreviousCamera;
    if(m_pRtCbvDataBegin)memcpy(m_pRtCbvDataBegin,&m_rtConstants,sizeof(m_rtConstants));
    m_dxrCommandList->SetPipelineState1(m_dxrStateObject.Get());
    m_dxrCommandList->DispatchRays(&dispatchDesc);
    if(m_giHistory){
        Transition(m_commandList.Get(),m_ssgiTexture.Get(),m_ssgiState,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(m_commandList.Get(),m_giHistory.Get(),m_giHistoryState,D3D12_RESOURCE_STATE_COPY_DEST);
        m_commandList->CopyResource(m_giHistory.Get(),m_ssgiTexture.Get());
        Transition(m_commandList.Get(),m_giHistory.Get(),m_giHistoryState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_giHistoryValid=m_ssgiEnabled&&m_ssgiIntensity>0&&!m_pathTracingEnabled;
        m_giHistoryLastFrame=m_frameCounter;
        XMStoreFloat4x4(&m_giPreviousViewProjection,m_viewProj);m_giPreviousCamera=m_rtConstants.cameraPos;
    }
    Transition(m_commandList.Get(),m_ssgiTexture.Get(),m_ssgiState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(m_commandList.Get(),m_rrHitDistance.Get(),m_rrHitDistanceState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    if (m_rayTracingOutput) {
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = m_rayTracingOutput.Get();
        m_commandList->ResourceBarrier(1, &uavBarrier);
        Transition(m_commandList.Get(), m_rayTracingOutput.Get(), m_rayTracingOutputState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_rayTracingOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

Renderer::Renderer(int width, int height)
    : m_width(width)
    , m_height(height)
    , m_renderWidth(width)
    , m_renderHeight(height)
    , m_rtvDescriptorSize(0)
    , m_cbvSrvDescriptorSize(0)
    , m_frameIndex(0)
    , m_fenceEvent(nullptr)
    , m_fenceValue(0)
    , m_frameCounter(0)
    , m_dxrEnabled(true)
    , m_rtShadowsEnabled(true)
    , m_rtReflectionsEnabled(true)
    , m_vsyncEnabled(true)
    , m_dlssSupported(false)
    , m_dlssActive(false)
    , m_fsr2Initialized(false)
    , m_upscaleTechnique(UpscaleTechnique::DLSS)
    , m_upscaleQuality(UpscaleQuality::Balanced)
    , m_upscaleSharpness(0.5f)
    , m_cloudDensity(0.8f)
    , m_nextSrvIndex(1)
    , m_skyColor(0.2f, 0.4f, 0.8f)
    , m_sunAzimuth(45.0f)
    , m_sunElevation(45.0f)
{
    m_viewProj = XMMatrixIdentity();
    m_cleanViewProj = XMMatrixIdentity();
    m_prevCleanViewProj = XMMatrixIdentity();
    
    m_constantBufferData = {};
    m_constantBufferData.lightDir = XMFLOAT3(0.5f, -1.0f, 0.5f);
    m_constantBufferData.lightColor = XMFLOAT3(1.0f, 0.9f, 0.8f);
    m_constantBufferData.cameraPos = XMFLOAT3(0.0f, 0.0f, 0.0f);

    UpdateSunFromAngles();
}

Renderer::~Renderer() {
    LOG("Tessellation draws: "<<m_tessellatedDraws);
    AdvancedShaderDelivery::ReportStatistics(m_device.Get());
    Destroy();
}

void Renderer::UpdateSunFromAngles() {
    float azRad = XMConvertToRadians(m_sunAzimuth);
    float elRad = XMConvertToRadians(m_sunElevation);

    float x = cos(elRad) * sin(azRad);
    float y = -sin(elRad);
    float z = cos(elRad) * cos(azRad);
    XMFLOAT3 dir = XMFLOAT3(x, y, z);
    float len = sqrt(x*x + y*y + z*z);
    if (len > 0.0001f) {
        dir.x /= len; dir.y /= len; dir.z /= len;
    } else {
        dir = XMFLOAT3(0, -1, 0);
    }
    SetLightDir(dir);

    float height = sin(elRad);
    float dayFactor = (height > 0.05f) ? (std::min)(1.0f, (height - 0.05f) * 2.5f) : 0.0f;
    float sunsetFactor = 0.0f;
    if (height > -0.08f && height < 0.30f) {
        float xNorm = (height - 0.05f) / 0.16f;
        sunsetFactor = exp(-xNorm * xNorm * 2.0f);
    }
    float twilightFactor = 0.0f;
    if (height <= 0.05f && height > -0.25f) {
        twilightFactor = (0.05f - height) / 0.30f;
    }
    float nightFactor = (height <= -0.25f) ? 1.0f : (height < 0.0f ? (-height / 0.25f) : 0.0f);

    float nightIntensity = (std::clamp)(1.0f - (std::clamp)((-height - 0.10f) / 0.35f, 0.0f, 1.0f), 0.0f, 1.0f);
    XMFLOAT3 dayColor = XMFLOAT3(1.0f, 0.98f, 0.95f);
    XMFLOAT3 sunsetColor = XMFLOAT3(1.0f, 0.55f, 0.20f);
    XMFLOAT3 twilightColor = XMFLOAT3(0.20f, 0.24f, 0.35f);
    XMFLOAT3 nightColor = XMFLOAT3(0.008f * nightIntensity, 0.012f * nightIntensity, 0.025f * nightIntensity);

    float colorX = nightColor.x * nightFactor + dayColor.x * dayFactor + sunsetColor.x * sunsetFactor * 0.8f + twilightColor.x * twilightFactor * 0.4f;
    float colorY = nightColor.y * nightFactor + dayColor.y * dayFactor + sunsetColor.y * sunsetFactor * 0.8f + twilightColor.y * twilightFactor * 0.4f;
    float colorZ = nightColor.z * nightFactor + dayColor.z * dayFactor + sunsetColor.z * sunsetFactor * 0.8f + twilightColor.z * twilightFactor * 0.4f;

    SetLightColor(XMFLOAT3((std::min)(1.0f, (std::max)(0.0f, colorX)), (std::min)(1.0f, (std::max)(0.0f, colorY)), (std::min)(1.0f, (std::max)(0.0f, colorZ))));

    // Dynamic Sky Color for skybox (neutral balanced atmospheric gradients)
    XMFLOAT3 daySky = XMFLOAT3(0.35f, 0.50f, 0.75f);
    XMFLOAT3 sunsetSky = XMFLOAT3(0.65f, 0.35f, 0.18f);
    XMFLOAT3 twilightSky = XMFLOAT3(0.10f, 0.14f, 0.24f);
    XMFLOAT3 nightSky = XMFLOAT3(0.005f * nightIntensity, 0.008f * nightIntensity, 0.015f * nightIntensity);

    m_skyColor.x = nightSky.x * nightFactor + daySky.x * dayFactor + sunsetSky.x * sunsetFactor * 0.7f + twilightSky.x * twilightFactor * 0.5f;
    m_skyColor.y = nightSky.y * nightFactor + daySky.y * dayFactor + sunsetSky.y * sunsetFactor * 0.7f + twilightSky.y * twilightFactor * 0.5f;
    m_skyColor.z = nightSky.z * nightFactor + daySky.z * dayFactor + sunsetSky.z * sunsetFactor * 0.7f + twilightSky.z * twilightFactor * 0.5f;
}

void Renderer::GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter) {
    *ppAdapter = nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIFactory6> factory6;
    bool found = false;

    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6)))) {
        for (UINT adapterIndex = 0;
             SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                 adapterIndex,
                 requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                 IID_PPV_ARGS(&adapter)));
             ++adapterIndex) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter.Reset();
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                found = true;
                break;
            }
            adapter.Reset();
        }
    }

    if (!found) {
        for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter.Reset();
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                found = true;
                break;
            }
            adapter.Reset();
        }
    }

    if (found) {
        *ppAdapter = adapter.Detach();
    }
}

#include "RenderBackend.inl"
#include "Tessellation.inl"

bool Renderer::Initialize(HWND hwnd, Camera* camera) {
    AdvancedShaderDelivery::SetIdentity();
    m_camera = camera;
    m_hwnd = hwnd;

    {
        // Streamline plugins live in ThirdParty/NVIDIA/Streamline.
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

        std::wstring nvidiaDir = exeDir + L"\\ThirdParty\\NVIDIA\\Streamline";
        SetDllDirectoryW(nvidiaDir.c_str());

        {
            auto tryLoad = [&](const wchar_t* dllName)
            {
                auto toUtf8 = [](const wchar_t* ws) -> std::string
                {
                    if (!ws) return {};
                    int required = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
                    if (required <= 0) return {};
                    std::string out;
                    out.resize(static_cast<size_t>(required - 1));
                    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), required, nullptr, nullptr);
                    return out;
                };

                std::wstring fullPath = nvidiaDir + L"\\" + dllName;
                HMODULE h = LoadLibraryW(fullPath.c_str());
                if (h)
                {
                    FreeLibrary(h);
                    LOG("Precheck LoadLibrary OK: " << toUtf8(fullPath.c_str()));
                }
                else
                {
                    DWORD err = GetLastError();
                    LOG("Precheck LoadLibrary FAILED: " << toUtf8(fullPath.c_str()) << " GetLastError=" << err);
                }
            };

            tryLoad(L"nvngx_dlss.dll");
            tryLoad(L"nvngx_dlssd.dll");
            tryLoad(L"nvngx_deepdvc.dll");
            tryLoad(L"nvngx_dlssg.dll");
        }

        sl::Preferences slPref = {};
        slPref.showConsole = false;
        slPref.logLevel = sl::LogLevel::eDefault;
        slPref.logMessageCallback=[](sl::LogType,const char* message){if(message)EngineLog::Write(std::string("[Streamline] ")+message);};
        slPref.applicationId = 1; 
        sl::Feature featuresToLoad[] = { sl::kFeatureDLSS, sl::kFeatureReflex, sl::kFeatureDLSS_G,sl::kFeaturePCL,sl::kFeatureDLSS_RR };
        slPref.featuresToLoad = featuresToLoad;
        slPref.numFeaturesToLoad = uint32_t(std::size(featuresToLoad));
        slPref.renderAPI = sl::RenderAPI::eD3D12;
        slPref.flags = sl::PreferenceFlags::eUseFrameBasedResourceTagging | sl::PreferenceFlags::eDisableDebugText;
        
        static std::wstring absPluginNvidia;
        static std::wstring absExeDir;
        absExeDir = exeDir;
        absPluginNvidia = nvidiaDir;
        const wchar_t* pluginPaths[] = { absPluginNvidia.c_str(), absExeDir.c_str() };
        slPref.pathsToPlugins = pluginPaths;
        slPref.numPathsToPlugins = 2;

        sl::Result slInitRes = slInit(slPref);
        m_streamlineInitialized=slInitRes==sl::Result::eOk;
        if (slInitRes != sl::Result::eOk)
        {
            LOG("Streamline slInit FAILED (code=" << (int)slInitRes << ")");
        }
    }

    UINT dxgiFactoryFlags = 0;

    m_heapPropsDefault = {};
    m_heapPropsDefault.Type = D3D12_HEAP_TYPE_DEFAULT;

    m_heapPropsUpload = {};
    m_heapPropsUpload.Type = D3D12_HEAP_TYPE_UPLOAD;

    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)))) {
        LOG("CreateDXGIFactory2 failed");
        return false;
    }
    m_dxgiFactory = factory;

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory.As(&factory5))) {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
            m_tearingSupported = (allowTearing == TRUE);
            LOG("DXGI Tearing Support: " << (m_tearingSupported ? "YES" : "NO"));
        }
    }

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    // Сначала создаём устройство
    if (FAILED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
        LOG("D3D12CreateDevice failed");
        return false;
    }

    m_pipelineCache.Initialize(m_device.Get());
    if (slSetD3DDevice(m_device.Get()) == sl::Result::eOk) {
        LOG("Streamline D3D Device Set");
        
        LUID adapterLuid = m_device->GetAdapterLuid();
        sl::AdapterInfo adapterInfo = {};
        adapterInfo.deviceLUID = (uint8_t*)&adapterLuid;
        adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

        if (slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk) {
            m_dlssSupported = true;
            LOG("DLSS Hardware: Supported");
        } else {
            m_dlssSupported = false;
            LOG("DLSS Hardware: Not Supported, falling back to Native/FSR2");
        }

        if (slIsFeatureSupported(sl::kFeatureDLSS_G, adapterInfo) == sl::Result::eOk) {
            m_dlssGSupported = true;
            sl::DLSSGState capabilities{};
            if(slDLSSGGetState(sl::ViewportHandle{0},capabilities,nullptr)==sl::Result::eOk)
                m_dlssGMaxMultiplier=(std::max)(2,int((std::min)(capabilities.numFramesToGenerateMax,31u))+1);
            LOG("DLSS-G Frame Generation Hardware: Supported");
        } else {
            m_dlssGSupported = false;
            LOG("DLSS-G Frame Generation Hardware: Not Supported");
        }

        if (slIsFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo) == sl::Result::eOk) {
            m_dlssRRSupported = true;
            LOG("DLSS-RR Ray Reconstruction Hardware: Supported");
        } else {
            m_dlssRRSupported = false;
            LOG("DLSS-RR Ray Reconstruction Hardware: Not Supported");
        }

        if (slIsFeatureSupported(sl::kFeatureReflex, adapterInfo) == sl::Result::eOk) {
            m_reflexSupported = true;
            LOG("Reflex Hardware: Supported");
            SetReflexMode(m_reflexMode);
        } else {
            m_reflexSupported = false;
            LOG("Reflex Hardware: Not Supported");
        }
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_5};
    if(FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shaderModel,sizeof(shaderModel)))||shaderModel.HighestShaderModel<D3D_SHADER_MODEL_6_5){LOG("Shader Model 6.5 is required");return false;}
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 meshOptions{};
    m_meshShadersAvailable=SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,&meshOptions,sizeof(meshOptions)))&&meshOptions.MeshShaderTier!=D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    if(BackendArgument(L"--benchmark-vertex-pipeline"))m_meshShadersAvailable=false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 rayOptions{};
    m_dxrAvailable=SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&rayOptions,sizeof(rayOptions)))&&rayOptions.RaytracingTier>=D3D12_RAYTRACING_TIER_1_1;
    if(BackendArgument(L"--benchmark-no-dxr-hardware"))m_dxrAvailable=false;
    if(m_dxrAvailable)m_device.As(&m_dxrDevice);else{m_dxrDevice.Reset();m_dxrEnabled=false;m_ssgiEnabled=false;}
    m_cullingProbeOutside=BackendArgument(L"--benchmark-cull-outside");
    m_buildDepthPyramid=BackendArgument(L"--benchmark-pipeline-depth-pyramid");
    InitializeBackendQueries();
    LOG("Render capabilities: mesh="<<m_meshShadersAvailable<<" dxr="<<m_dxrAvailable<<" shaderModel="<<int(shaderModel.HighestShaderModel));

    if (m_dxrDevice) {
        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        UINT cbSize = (sizeof(RaytracingConstants) + 255) & ~255;
        m_rtConstantBuffer = CreateBuffer(cbSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, uploadHeap);
        if (m_rtConstantBuffer) m_rtConstantBuffer->Map(0, nullptr, &m_pRtCbvDataBegin);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)))) {
        LOG("CreateCommandQueue failed");
        return false;
    }

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) return false;
    

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain;
    if (FAILED(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ))) return false;

    if (FAILED(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER))) return false;

    swapChain.As(&m_swapChain);
    swapChain.Reset();AttachStreamlinePresentation();

    if (!m_swapChain) {
                return false;
    }



    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 32;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 4;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)))) return false;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    // Create RTVs for swap chain backbuffers
    for (UINT n = 0; n < FrameCount; n++) {
        if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])))) {
            LOG("Init: GetBuffer(" << n << ") failed!");
            return false;
        }
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        std::wstring name = L"BackBuffer_" + std::to_wstring(n);
        m_renderTargets[n]->SetName(name.c_str());
        rtvHandle.ptr += m_rtvDescriptorSize;
        LOG("Init: Created RTV for BackBuffer " << n);
    }

    m_sceneColorRtvIndex = FrameCount;
    m_compositedColorRtvIndex = FrameCount + 1;
    m_velocityRtvIndex = FrameCount + 2;
    m_upscaledColorRtvIndex = FrameCount + 3;
    m_ssaoRtvIndex = FrameCount + 4;
    m_ssaoBlurRtvIndex = FrameCount + 5;
    m_mboitMomentsRtvIndex = FrameCount + 6;
    m_rayTracingOutputRtvIndex = FrameCount + 7;
    m_vsmPhysicalAtlasRtvIndex = FrameCount + 8;
    m_rvtAlbedoAtlasRtvIndex = FrameCount + 9;
    m_rvtNormalAtlasRtvIndex = FrameCount + 10;
    m_rvtIrradianceAtlasRtvIndex = FrameCount + 11;
    m_normalRtvIndex = FrameCount + 12;

        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)))) return false;
    LOG("Creating Command List. Device: " << m_device.Get() << ", Allocator: " << m_commandAllocator.Get());
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)))) {
                return false;
    }
    if (m_commandList) m_commandList->SetName(L"MainCommandList");
    m_commandList.As(&m_dxrCommandList);
    m_commandList->Close();

    D3D12_ROOT_PARAMETER1 rootParameters[7] = {};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor = { 0, 0 };
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1000;
    range.BaseShaderRegister = 0;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    range.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &range;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[2].Descriptor = { 0, 1 }; // t0, space 1
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[3].Descriptor = { 1, 1 }; // t1, space 1
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParameters[4].Descriptor = { 2, 1 }; // t2, space 1
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE1 space1Range = {};
    space1Range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    space1Range.NumDescriptors = 8; // t3 to t10
    space1Range.BaseShaderRegister = 3;
    space1Range.RegisterSpace = 1;
    space1Range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    space1Range.OffsetInDescriptorsFromTableStart = 0;

    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges = &space1Range;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
    
    // Sampler 0: register(s0, space0)
    samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxAnisotropy = 16;
    samplers[0].ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MipLODBias = -1.0f;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Sampler 1: register(s1, space0) - Linear sampler for atlases / fog
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[2]=samplers[1];samplers[2].ShaderRegister=2;samplers[2].Filter=D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;samplers[2].ComparisonFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;samplers[2].MaxLOD=D3D12_FLOAT32_MAX;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


    rootParameters[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[6].Constants={1,0,4};
    rootParameters[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;

    // Detect highest supported Root Signature version
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
    m_highestRootSignatureVersion = featureData.HighestVersion;
    LOG("Root Signature Version Supported: " << (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 ? "1.1" : "1.0"));

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    
    if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1.NumParameters = 7;
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 3;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = samplers;
        rootSignatureDesc.Desc_1_1.Flags = rootSignatureFlags;
    } else {
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        static std::vector<D3D12_ROOT_PARAMETER> rootParamsV10(7);
        for (int i = 0; i < 7; ++i) {
            rootParamsV10[i].ParameterType = rootParameters[i].ParameterType;
            rootParamsV10[i].ShaderVisibility = rootParameters[i].ShaderVisibility;
            if (rootParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
                rootParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
                rootParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) {
                rootParamsV10[i].Descriptor.ShaderRegister = rootParameters[i].Descriptor.ShaderRegister;
                rootParamsV10[i].Descriptor.RegisterSpace = rootParameters[i].Descriptor.RegisterSpace;
            } else if(rootParameters[i].ParameterType==D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS){
                rootParamsV10[i].Constants=rootParameters[i].Constants;
            } else if (rootParameters[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                rootParamsV10[i].DescriptorTable.NumDescriptorRanges = rootParameters[i].DescriptorTable.NumDescriptorRanges;
                static D3D12_DESCRIPTOR_RANGE tableRanges[7][1];
                for(UINT r=0; r<rootParameters[i].DescriptorTable.NumDescriptorRanges; ++r) {
                    tableRanges[i][r].RangeType = rootParameters[i].DescriptorTable.pDescriptorRanges[r].RangeType;
                    tableRanges[i][r].NumDescriptors = rootParameters[i].DescriptorTable.pDescriptorRanges[r].NumDescriptors;
                    tableRanges[i][r].BaseShaderRegister = rootParameters[i].DescriptorTable.pDescriptorRanges[r].BaseShaderRegister;
                    tableRanges[i][r].RegisterSpace = rootParameters[i].DescriptorTable.pDescriptorRanges[r].RegisterSpace;
                    tableRanges[i][r].OffsetInDescriptorsFromTableStart = rootParameters[i].DescriptorTable.pDescriptorRanges[r].OffsetInDescriptorsFromTableStart;
                }
                rootParamsV10[i].DescriptorTable.pDescriptorRanges = tableRanges[i];
            }
        }
        rootSignatureDesc.Desc_1_0.NumParameters = 7;
        rootSignatureDesc.Desc_1_0.pParameters = rootParamsV10.data();
        rootSignatureDesc.Desc_1_0.NumStaticSamplers = 3;
        rootSignatureDesc.Desc_1_0.pStaticSamplers = samplers;
        rootSignatureDesc.Desc_1_0.Flags = rootSignatureFlags;
    }

    HRESULT hrInit = CreateRootSignature(&rootSignatureDesc, &m_rootSignature);
    if (FAILED(hrInit)) {
        LOG("CreateRootSignature main failed with HRESULT: 0x" << std::hex << hrInit);
        return false;
    }

    // 2. Compile Shaders
    ComPtr<IDxcBlob> amplificationShader;
    ComPtr<IDxcBlob> meshShader;
    ComPtr<IDxcBlob> pixelShader;

    // Set runtime compile fallback to true for debugging
    _putenv("ENABLE_DXC_RUNTIME_COMPILE=1");
    
    amplificationShader = CompileShaderDXC(L"Contents/shaders/shaders.hlsl", L"ASMain", L"as_6_5");
    if (!amplificationShader) { LOG("AS compilation failed"); return false; }
    LOG("AS compilation OK");

    meshShader = CompileShaderDXC(L"Contents/shaders/shaders.hlsl", L"MSMain", L"ms_6_5");
    if (!meshShader) { LOG("MS compilation failed"); return false; }
    LOG("MS compilation OK");

    pixelShader = CompileShaderDXC(L"Contents/shaders/shaders.hlsl", L"PSMain", L"ps_6_5");
    if (!pixelShader) { LOG("PS compilation failed"); return false; }
    LOG("PS compilation OK");

    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rasterDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    rasterDesc.ForcedSampleCount = 0;
    rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
        FALSE,FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

    if(m_meshShadersAvailable){
    MeshShaderPipelineStream psoStream = {};
    psoStream.RootSignature.pRootSignature = m_rootSignature.Get();
    psoStream.AS.AS = { amplificationShader->GetBufferPointer(), amplificationShader->GetBufferSize() };
    psoStream.MS.MS = { meshShader->GetBufferPointer(), meshShader->GetBufferSize() };
    psoStream.PS.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoStream.RasterizerState.RasterizerState = rasterDesc;
    psoStream.BlendState.BlendState = blendDesc;
    psoStream.DepthStencilState.DepthStencilState.DepthEnable = TRUE;
    psoStream.DepthStencilState.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoStream.DepthStencilState.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoStream.DepthStencilState.DepthStencilState.StencilEnable = FALSE;
    psoStream.SampleMask.SampleMask = 0xFFFFFFFF;
    psoStream.PrimitiveTopologyType.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoStream.RTVFormats.RTVFormats.NumRenderTargets = 3;
    psoStream.RTVFormats.RTVFormats.RTFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoStream.RTVFormats.RTVFormats.RTFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    psoStream.RTVFormats.RTVFormats.RTFormats[2] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoStream.DSVFormat.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoStream.SampleDesc.SampleDesc = { 1, 0 };

    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc = {};
    streamDesc.SizeInBytes = sizeof(psoStream);
    streamDesc.pPipelineStateSubobjectStream = &psoStream;

    HRESULT hrPSO = m_pipelineCache.Mesh("mesh-main",&streamDesc,m_rootSignature.Get(),{psoStream.AS.AS,psoStream.MS.MS,psoStream.PS.PS},IID_PPV_ARGS(&m_pipelineState));
    psoStream.RTVFormats.RTVFormats.NumRenderTargets=5;psoStream.RTVFormats.RTVFormats.RTFormats[3]=DXGI_FORMAT_R16G16B16A16_FLOAT;psoStream.RTVFormats.RTVFormats.RTFormats[4]=DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto rrPipelineResult=m_pipelineCache.Mesh("mesh-guides",&streamDesc,m_rootSignature.Get(),{psoStream.AS.AS,psoStream.MS.MS,psoStream.PS.PS},IID_PPV_ARGS(&m_rrPipelineState));
    if(FAILED(rrPipelineResult)){LOG("ERROR: RR pipeline creation failed: 0x"<<std::hex<<rrPipelineResult);return false;}
    psoStream.RTVFormats.RTVFormats.NumRenderTargets=3;

    if (FAILED(hrPSO) || !m_pipelineState) {
        LOG("ERROR: Main PSO creation failed with HRESULT: 0x" << std::hex << hrPSO);
        
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            UINT64 numMsgs = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 i = 0; i < numMsgs; ++i) {
                SIZE_T msgLen = 0;
                infoQueue->GetMessage(i, nullptr, &msgLen);
                D3D12_MESSAGE* msg = (D3D12_MESSAGE*)malloc(msgLen);
                if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
                    LOG("D3D12 Debug: " << msg->pDescription);
                }
                free(msg);
            }
            infoQueue->ClearStoredMessages();
        }
        return false;
    }
    LOG("Main PSO created successfully");

    // Shadow Pass PSO (Mesh Shader Depth-Only)
    {
        MeshShaderPipelineStream vsmShadowStream = {};
        vsmShadowStream.RootSignature.pRootSignature = m_rootSignature.Get();
        vsmShadowStream.AS.AS = { amplificationShader->GetBufferPointer(), amplificationShader->GetBufferSize() };
        vsmShadowStream.MS.MS = { meshShader->GetBufferPointer(), meshShader->GetBufferSize() };
        vsmShadowStream.PS.PS = { nullptr, 0 };
        
        D3D12_RASTERIZER_DESC shadowRaster = rasterDesc;
        shadowRaster.CullMode = D3D12_CULL_MODE_NONE;
        shadowRaster.DepthBias = 50;
        shadowRaster.DepthBiasClamp = 0.0f;
        shadowRaster.SlopeScaledDepthBias = 1.0f;
        vsmShadowStream.RasterizerState.RasterizerState = shadowRaster;

        vsmShadowStream.BlendState.BlendState = blendDesc;
        vsmShadowStream.DepthStencilState.DepthStencilState.DepthEnable = TRUE;
        vsmShadowStream.DepthStencilState.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        vsmShadowStream.DepthStencilState.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        vsmShadowStream.DepthStencilState.DepthStencilState.StencilEnable = FALSE;
        vsmShadowStream.SampleMask.SampleMask = 0xFFFFFFFF;
        vsmShadowStream.PrimitiveTopologyType.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        vsmShadowStream.RTVFormats.RTVFormats.NumRenderTargets = 0;
        vsmShadowStream.DSVFormat.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        vsmShadowStream.SampleDesc.SampleDesc = { 1, 0 };

        D3D12_PIPELINE_STATE_STREAM_DESC vsmStreamDesc = {};
        vsmStreamDesc.SizeInBytes = sizeof(vsmShadowStream);
        vsmStreamDesc.pPipelineStateSubobjectStream = &vsmShadowStream;

        HRESULT hrShadow = m_pipelineCache.Mesh("mesh-shadow",&vsmStreamDesc,m_rootSignature.Get(),{vsmShadowStream.AS.AS,vsmShadowStream.MS.MS},IID_PPV_ARGS(&m_vsmShadowPipelineState));
        if (FAILED(hrShadow)) {
            LOG("Shadow: Mesh Shader PSO creation failed: 0x" << std::hex << hrShadow);
            LogD3D12Messages();
        } else {
            LOG("Shadow: Mesh Shader PSO Created successfully!");
        }
    }

    }else if(!InitializeVertexPipeline())return false;

    if(!InitializeTessellation()){LOG("Tessellation pipeline initialization failed");return false;}

    if (FAILED(m_commandAllocator->Reset())) return false;
    if (FAILED(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()))) return false;

    float s = 500.0f;
    std::vector<Renderer::Vertex> verts;
    std::vector<uint16_t> idx;

    auto V = [&](Renderer::Vertex v) { verts.push_back(v); };
    auto T = [&](uint16_t a, uint16_t b, uint16_t c) {
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
    };

    m_floorBaseVertex = (INT)verts.size();
    m_floorStartIndex = (UINT)idx.size();
    UINT floorVBase = (UINT)m_floorBaseVertex;
    UINT floorIdxStart = m_floorStartIndex;
    V({ { -s, 0, -s }, { 0,1,0 }, { 1,1,1,1 } });
    V({ { -s, 0,  s }, { 0,1,0 }, { 1,1,1,1 } });
    V({ {  s, 0,  s }, { 0,1,0 }, { 1,1,1,1 } });
    V({ {  s, 0, -s }, { 0,1,0 }, { 1,1,1,1 } });
    T(0,1,2); T(0,2,3);
    m_floorIndexCount = (UINT)idx.size() - floorIdxStart;
    m_floorVertexCount = (UINT)verts.size() - floorVBase;
    UINT floorIdxCount = m_floorIndexCount;
    UINT floorVertexCount = m_floorVertexCount;

    // --- House Walls (4 separate faces with flat outward normals) ---
    m_wallBaseVertex = (INT)verts.size();
    m_wallStartIndex = (UINT)idx.size();
    UINT wallVBase = (UINT)m_wallBaseVertex;
    UINT wallIdxStart = m_wallStartIndex;
    float bx = 3.0f; // 6m wide house
    float bz = 3.5f; // 7m deep house
    float bh = 3.5f; // 3.5m wall height
    XMFLOAT4 wallCol = { 0.88f, 0.85f, 0.80f, 1.0f };

    // Front Wall (+Z face)
    {
        uint16_t b = (uint16_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 0.0f, 0.0f, 1.0f };
        V({{ -bx, 0.0f, bz }, n, wallCol });
        V({{  bx, 0.0f, bz }, n, wallCol });
        V({{  bx,   bh, bz }, n, wallCol });
        V({{ -bx,   bh, bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Back Wall (-Z face)
    {
        uint16_t b = (uint16_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 0.0f, 0.0f, -1.0f };
        V({{  bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx,   bh, -bz }, n, wallCol });
        V({{  bx,   bh, -bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Left Wall (-X face)
    {
        uint16_t b = (uint16_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { -1.0f, 0.0f, 0.0f };
        V({{ -bx, 0.0f, -bz }, n, wallCol });
        V({{ -bx, 0.0f,  bz }, n, wallCol });
        V({{ -bx,   bh,  bz }, n, wallCol });
        V({{ -bx,   bh, -bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    // Right Wall (+X face)
    {
        uint16_t b = (uint16_t)(verts.size() - wallVBase);
        XMFLOAT3 n = { 1.0f, 0.0f, 0.0f };
        V({{  bx, 0.0f,  bz }, n, wallCol });
        V({{  bx, 0.0f, -bz }, n, wallCol });
        V({{  bx,   bh, -bz }, n, wallCol });
        V({{  bx,   bh,  bz }, n, wallCol });
        T(b, b+1, b+2); T(b, b+2, b+3);
    }
    m_wallIndexCount = (UINT)idx.size() - wallIdxStart;
    m_wallVertexCount = (UINT)verts.size() - wallVBase;
    UINT wallIdxCount = m_wallIndexCount;
    UINT wallVertexCount = m_wallVertexCount;

    // --- House Roof (4 separate triangular faces with true outward flat normals) ---
    m_roofBaseVertex = (INT)verts.size();
    m_roofStartIndex = (UINT)idx.size();
    UINT roofVBase = (UINT)m_roofBaseVertex;
    UINT roofIdxStart = m_roofStartIndex;
    float rx = bx + 0.4f; // Overhang
    float rz = bz + 0.4f;
    float rBaseY = bh - 0.05f;
    float rPeakY = bh + 2.2f;
    XMFLOAT4 roofCol = { 0.75f, 0.28f, 0.18f, 1.0f }; // Warm terracotta roof tiles

    auto addRoofFace = [&](XMFLOAT3 v0, XMFLOAT3 v1, XMFLOAT3 v2) {
        uint16_t b = (uint16_t)(verts.size() - roofVBase);
        XMVECTOR e1 = XMVectorSubtract(XMLoadFloat3(&v1), XMLoadFloat3(&v0));
        XMVECTOR e2 = XMVectorSubtract(XMLoadFloat3(&v2), XMLoadFloat3(&v0));
        XMVECTOR nVec = XMVector3Normalize(XMVector3Cross(e1, e2));
        XMFLOAT3 n; XMStoreFloat3(&n, nVec);
        V({ v0, n, roofCol });
        V({ v1, n, roofCol });
        V({ v2, n, roofCol });
        T(b, b+1, b+2);
    };

    // Front Slope (+Z)
    addRoofFace({ -rx, rBaseY, rz }, { rx, rBaseY, rz }, { 0.0f, rPeakY, 0.0f });
    // Back Slope (-Z)
    addRoofFace({ rx, rBaseY, -rz }, { -rx, rBaseY, -rz }, { 0.0f, rPeakY, 0.0f });
    // Left Slope (-X)
    addRoofFace({ -rx, rBaseY, -rz }, { -rx, rBaseY, rz }, { 0.0f, rPeakY, 0.0f });
    // Right Slope (+X)
    addRoofFace({ rx, rBaseY, rz }, { rx, rBaseY, -rz }, { 0.0f, rPeakY, 0.0f });

    m_roofIndexCount = (UINT)idx.size() - roofIdxStart;
    m_roofVertexCount = (UINT)verts.size() - roofVBase;
    UINT roofIdxCount = m_roofIndexCount;
    UINT roofVertexCount = m_roofVertexCount;

    m_cubeBaseVertex = (INT)verts.size();
    m_cubeStartIndex = (UINT)idx.size();
    {
        struct Face { XMFLOAT3 n, r, u; XMFLOAT4 col; };
        Face faces[] = {
            {{ 0, 0,-1},{-1, 0, 0},{ 0, 1, 0}, {0.90f,0.90f,0.90f,1.0f}},
            {{ 0, 0, 1},{ 1, 0, 0},{ 0, 1, 0}, {0.65f,0.65f,0.65f,1.0f}},
            {{-1, 0, 0},{ 0, 0, 1},{ 0, 1, 0}, {0.75f,0.75f,0.75f,1.0f}},
            {{ 1, 0, 0},{ 0, 0,-1},{ 0, 1, 0}, {0.75f,0.75f,0.75f,1.0f}},
            {{ 0, 1, 0},{-1, 0, 0},{ 0, 0, 1}, {0.85f,0.85f,0.85f,1.0f}},
            {{ 0,-1, 0},{ 1, 0, 0},{ 0, 0, 1}, {0.50f,0.50f,0.50f,1.0f}},
        };
        for (int f = 0; f < 6; f++) {
            XMFLOAT3 n=faces[f].n, r=faces[f].r, u=faces[f].u;
            XMFLOAT4 col=faces[f].col;
            uint16_t b = (uint16_t)(verts.size() - m_cubeBaseVertex);
            auto c = [&](float rs, float us) {
                return Renderer::Vertex{{
                    n.x*.5f + r.x*rs*.5f + u.x*us*.5f,
                    n.y*.5f + r.y*rs*.5f + u.y*us*.5f,
                    n.z*.5f + r.z*rs*.5f + u.z*us*.5f },
                    n, col};
            };
            V(c(-1,-1)); V(c(1,-1)); V(c(1,1)); V(c(-1,1));
            T(b,b+1,b+2); T(b,b+2,b+3);
        }
    }
    m_cubeIndexCount = (UINT)idx.size() - m_cubeStartIndex;
    m_cubeVertexCount = (UINT)verts.size() - (UINT)m_cubeBaseVertex;

    m_planeBaseVertex = (INT)verts.size();
    m_planeStartIndex = (UINT)idx.size();
    V({{ -0.5f, 0, -0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{ -0.5f, 0,  0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{  0.5f, 0,  0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    V({{  0.5f, 0, -0.5f }, { 0,1,0 }, { 0.75f, 0.75f, 0.75f, 1.0f }});
    T(0,1,2); T(0,2,3);
    T(0,2,1); T(0,3,2);
    m_planeIndexCount = (UINT)idx.size() - m_planeStartIndex;
    m_planeVertexCount = (UINT)verts.size() - (UINT)m_planeBaseVertex;

    m_triangleBaseVertex = (INT)verts.size();
    m_triangleStartIndex = (UINT)idx.size();

    auto faceN = [](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c) -> XMFLOAT3 {
        XMVECTOR va = XMLoadFloat3(&a), vb = XMLoadFloat3(&b), vc = XMLoadFloat3(&c);
        XMVECTOR n = XMVector3Normalize(XMVector3Cross(XMVectorSubtract(vb,va), XMVectorSubtract(vc,va)));
        XMFLOAT3 r; XMStoreFloat3(&r,n); return r;
    };

    {
        XMFLOAT3 tv0 = { 0.0f,  0.5f,  0.0f };
        XMFLOAT3 tv1 = { 0.0f, -0.25f,  0.5f };
        XMFLOAT3 tv2 = { 0.433f,-0.25f,-0.25f };
        XMFLOAT3 tv3 = {-0.433f,-0.25f,-0.25f };

        XMFLOAT3 n0=faceN(tv0,tv1,tv2); uint16_t b0=(uint16_t)(verts.size()-m_triangleBaseVertex);
        V({tv0,n0,{0.80f,0.75f,0.30f,1.0f}}); V({tv1,n0,{0.80f,0.75f,0.30f,1.0f}}); V({tv2,n0,{0.80f,0.75f,0.30f,1.0f}});
        T(b0,b0+1,b0+2);

        XMFLOAT3 n1=faceN(tv0,tv2,tv3); uint16_t b1=(uint16_t)(verts.size()-m_triangleBaseVertex);
        V({tv0,n1,{0.80f,0.75f,0.30f,1.0f}}); V({tv2,n1,{0.80f,0.75f,0.30f,1.0f}}); V({tv3,n1,{0.80f,0.75f,0.30f,1.0f}});
        T(b1,b1+1,b1+2);

        XMFLOAT3 n2=faceN(tv0,tv3,tv1); uint16_t b2=(uint16_t)(verts.size()-m_triangleBaseVertex);
        V({tv0,n2,{0.80f,0.75f,0.30f,1.0f}}); V({tv3,n2,{0.80f,0.75f,0.30f,1.0f}}); V({tv1,n2,{0.80f,0.75f,0.30f,1.0f}});
        T(b2,b2+1,b2+2);

        XMFLOAT3 n3=faceN(tv1,tv3,tv2); uint16_t b3=(uint16_t)(verts.size()-m_triangleBaseVertex);
        V({tv1,n3,{0.80f,0.75f,0.30f,1.0f}}); V({tv3,n3,{0.80f,0.75f,0.30f,1.0f}}); V({tv2,n3,{0.80f,0.75f,0.30f,1.0f}});
        T(b3,b3+1,b3+2);
    }
    m_triangleIndexCount = (UINT)idx.size() - m_triangleStartIndex;
    m_triangleVertexCount = (UINT)verts.size() - (UINT)m_triangleBaseVertex;

    m_sphereBaseVertex = (INT)verts.size();
    m_sphereStartIndex = (UINT)idx.size();
    {
        const int stacks=12, slices=16;
        for (int st=0; st<=stacks; st++) {
            float phi = XM_PI * st / stacks;
            for (int sl=0; sl<=slices; sl++) {
                float theta = 2*XM_PI*sl/slices;
                float x=sinf(phi)*cosf(theta), y=cosf(phi), z=sinf(phi)*sinf(theta);
                float uCoord = (float)sl / (float)slices;
                float vCoord = (float)st / (float)stacks;
                V({{ x*.5f, y*.5f, z*.5f }, {x,y,z}, {0.75f,0.75f, uCoord, vCoord}});
            }
        }
        for (int st=0; st<stacks; st++) {
            for (int sl=0; sl<slices; sl++) {
                uint16_t a = (uint16_t)(st*(slices+1)+sl);
                uint16_t b = a + (slices+1);
                T(a,b,a+1); T(a+1,b,b+1);
            }
        }
    }
    m_sphereIndexCount = (UINT)idx.size() - m_sphereStartIndex;
    m_sphereVertexCount = (UINT)verts.size() - (UINT)m_sphereBaseVertex;

    m_constantBufferData.lightDir = XMFLOAT3(-0.6f, -0.7f, -0.3f);
    m_constantBufferData.lightColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
    m_constantBufferData.cameraPos = XMFLOAT3(0, 1, -5);
    m_constantBufferData.albedoIndex = -1;
    m_constantBufferData.normalIndex = -1;
    m_constantBufferData.rmaIndex = -1;

    const UINT vertexBufferSize = (UINT)(verts.size() * sizeof(Renderer::Vertex));
    const UINT indexBufferSize  = (UINT)(idx.size()  * sizeof(uint32_t));

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Alignment = 0;
    vbDesc.Width = vertexBufferSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.SampleDesc.Quality = 0;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)))) return false;
    if (m_vertexBuffer) m_vertexBuffer->SetName(L"GeometryVertexBuffer");

    LOG("VertexBuffer size: " << vertexBufferSize << ", IndexBuffer size: " << indexBufferSize);
    LOG("Cube: startIndex=" << m_cubeStartIndex << ", indexCount=" << m_cubeIndexCount);

    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = {0, 0};
    if (FAILED(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)))) return false;
    memcpy(pVertexDataBegin, verts.data(), verts.size() * sizeof(Renderer::Vertex));
    m_vertexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Renderer::Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = indexBufferSize;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)))) return false;
    if (m_indexBuffer) m_indexBuffer->SetName(L"GeometryIndexBuffer");

    std::vector<uint32_t> idx32;
    UINT8* pIndexDataBegin;
    if (FAILED(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)))) return false;
    idx32.assign(idx.begin(), idx.end());
    memcpy(pIndexDataBegin, idx32.data(), idx32.size() * sizeof(uint32_t));
    m_indexBuffer->Unmap(0, nullptr);

    m_staticVertices = verts;
    m_staticIndices = idx32;

    LOG("Total vertices: " << verts.size() << ", Total indices: " << idx32.size());

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = indexBufferSize;

    LOG("Adding models to scene...");
    m_models.emplace_back("Floor", floorIdxCount, floorIdxStart, (INT)floorVBase, floorVertexCount);

    RenderModel wallsModel("House Walls", wallIdxCount, wallIdxStart, (INT)wallVBase, wallVertexCount);
    wallsModel.position = { 0.0f, 0.0f, 8.0f };
    wallsModel.material.albedo = { 0.88f, 0.85f, 0.80f };
    wallsModel.material.roughness = 0.65f;
    wallsModel.material.metallic = 0.0f;
    m_models.push_back(wallsModel);

    RenderModel roofModel("House Roof", roofIdxCount, roofIdxStart, (INT)roofVBase, roofVertexCount);
    roofModel.position = { 0.0f, 0.0f, 8.0f };
    roofModel.material.albedo = { 0.75f, 0.28f, 0.18f };
    roofModel.material.roughness = 0.50f;
    roofModel.material.metallic = 0.0f;
    m_models.push_back(roofModel);

    LOG("Models added, building per-model isolated meshlets...");
    UpdateModelMeshletRanges();
    LOG("Meshlets built successfully: " << m_meshletCount);

    D3D12_RESOURCE_DESC cbDesc = vbDesc;
    cbDesc.Width = 1024 * 1024;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)))) return false;
    if (m_constantBuffer) m_constantBuffer->SetName(L"MainConstantBuffer");

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    m_constantBufferData.screenRes = XMFLOAT2((float)m_renderWidth, (float)m_renderHeight);
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = (sizeof(SceneConstantBuffer) + 255) & ~255;
    
    D3D12_DESCRIPTOR_HEAP_DESC mainHeapDesc = {};
    mainHeapDesc.NumDescriptors = 8192;
    mainHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    mainHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    if (FAILED(m_device->CreateDescriptorHeap(&mainHeapDesc, IID_PPV_ARGS(&m_mainDescHeap)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = 16;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)))) return false;

    D3D12_SAMPLER_DESC rtSamplerDesc = {};
    rtSamplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
    rtSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    rtSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    rtSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    rtSamplerDesc.MipLODBias = 0.0f;
    rtSamplerDesc.MaxAnisotropy = 16;
    rtSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    rtSamplerDesc.MinLOD = 0.0f;
    rtSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

    m_device->CreateSampler(&rtSamplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
    
    m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
    nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE mainHeapStart = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 1; i <= 1000; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = mainHeapStart;
        handle.ptr += i * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(nullptr, &nullSrvDesc, handle);
    }
    m_nextSrvIndex = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateConstantBufferView(&cbvDesc, mainHeapHandle);

    m_sceneColorSrvIndex = 1;
    m_compositedColorSrvIndex = 2;
    m_velocitySrvIndex = 3;
    m_upscaledColorSrvIndex = 4;
    m_ssaoSrvIndex = 5;
    m_ssaoBlurSrvIndex = 6;
    m_depthSrvIndex = 7;
    m_mboitMomentsSrvIndex = 8;
    m_rayTracingOutputSrvIndex = 9;
    m_rayTracingOutputUavIndex = 2000; 

    // Space 1 Textures Block (matches Param 5: t3-t10, space 1)
    m_hizSrvIndex = 1500;               // t3
    m_vsmPhysicalAtlasSrvIndex = 1501;  // t4
    m_vsmPageTableSrvIndex = 1502;      // t5
    m_rvtAlbedoAtlasSrvIndex = 1503;    // t6
    m_rvtNormalAtlasSrvIndex = 1504;    // t7
    m_rvtIrradianceAtlasSrvIndex = 1505;// t8
    m_rvtPageTableSrvIndex = 1506;      // t9
    m_volumetricFogSrvIndex = 1507;     // t10

    m_ssgiSrvIndex = 12;
    m_meshletSrvIndex = 13;
    m_normalSrvIndex = 14;
    m_ssgiUavIndex = 2001;
    m_hizUavIndex = 2002; 
    m_vsmPageTableUavIndex = 2015;
    m_vsmPhysicalAtlasUavIndex = 2016;
    m_rvtAlbedoAtlasUavIndex = 2017;
    m_rvtNormalAtlasUavIndex = 2018;
    m_rvtIrradianceAtlasUavIndex = 2019;
    m_rvtPageTableUavIndex = 2020;
    m_historyColorSrvIndex = 21;
    m_historyColorUavIndex = 2021;
    m_volumetricFogUavIndex = 2022;
    m_volumetricFogAccumulationUavIndex = 2023;

    // Reflection Indices
    m_reflectionSrvIndex = 23;
    m_reflectionUavIndex = 2024;

    m_nullSrvIndex = 22;
    m_whiteSrvIndex = 25;
    m_nextSrvIndex = 26; 

    for (UINT i = 1; i < 2048; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = mainHeapHandle;
        handle.ptr += i * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(nullptr, &nullSrvDesc, handle);
    }

    // 1x1 White Texture
    {
        D3D12_RESOURCE_DESC whiteDesc = {};
        whiteDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        whiteDesc.Width = 1;
        whiteDesc.Height = 1;
        whiteDesc.DepthOrArraySize = 1;
        whiteDesc.MipLevels = 1;
        whiteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        whiteDesc.SampleDesc.Count = 1;
        whiteDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &whiteDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_whiteTextureResource));

        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = 256;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> whiteUpload;
        m_device->CreateCommittedResource(
            &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&whiteUpload));

        uint32_t* pPixel = nullptr;
        D3D12_RANGE rRange = {0, 0};
        if (whiteUpload && SUCCEEDED(whiteUpload->Map(0, &rRange, reinterpret_cast<void**>(&pPixel)))) {
            *pPixel = 0xFFFFFFFF;
            whiteUpload->Unmap(0, nullptr);
        }

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_whiteTextureResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = whiteUpload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = 1;
        src.PlacedFootprint.Footprint.Height = 1;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = 256;

        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_whiteTextureResource.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &b);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvH = mainHeapHandle;
        srvH.ptr += m_whiteSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_whiteTextureResource.Get(), &srvDesc, srvH);
    }

    D3D12_RANGE cbvReadRange = {0, 0};
    if (FAILED(m_constantBuffer->Map(0, &cbvReadRange, reinterpret_cast<void**>(&m_pCbvDataBegin)))) return false;

    m_textureStreamer = std::make_unique<TextureStreamer>(this, m_device.Get());
    if (!m_textureStreamer->Initialize()) return false;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 2;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    if (FAILED(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)))) {  return false; }

    D3D12_ROOT_PARAMETER1 skyboxParams[1];
    skyboxParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    skyboxParams[0].Descriptor = { 0, 0 };
    skyboxParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    D3D12_STATIC_SAMPLER_DESC skyboxSampler = {};
    skyboxSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    skyboxSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    skyboxSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    skyboxSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    skyboxSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    skyboxSampler.MaxLOD = D3D12_FLOAT32_MAX;
    skyboxSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_FLAGS skyboxRootFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC skyboxRootDesc = {};
    skyboxRootDesc.Version = m_highestRootSignatureVersion;
    if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
        skyboxRootDesc.Desc_1_1.NumParameters = 1;
        skyboxRootDesc.Desc_1_1.pParameters = skyboxParams;
        skyboxRootDesc.Desc_1_1.NumStaticSamplers = 1;
        skyboxRootDesc.Desc_1_1.pStaticSamplers = &skyboxSampler;
        skyboxRootDesc.Desc_1_1.Flags = skyboxRootFlags;
    } else {
        static D3D12_ROOT_PARAMETER skyboxParamsV10[1];
        skyboxParamsV10[0].ParameterType = skyboxParams[0].ParameterType;
        skyboxParamsV10[0].Descriptor.ShaderRegister = skyboxParams[0].Descriptor.ShaderRegister;
        skyboxParamsV10[0].Descriptor.RegisterSpace = skyboxParams[0].Descriptor.RegisterSpace;
        skyboxParamsV10[0].ShaderVisibility = skyboxParams[0].ShaderVisibility;

        skyboxRootDesc.Desc_1_0.NumParameters = 1;
        skyboxRootDesc.Desc_1_0.pParameters = skyboxParamsV10;
        skyboxRootDesc.Desc_1_0.NumStaticSamplers = 1;
        static D3D12_STATIC_SAMPLER_DESC skyboxSamplerV10;
        memcpy(&skyboxSamplerV10, &skyboxSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
        skyboxRootDesc.Desc_1_0.pStaticSamplers = &skyboxSamplerV10;
        skyboxRootDesc.Desc_1_0.Flags = skyboxRootFlags;
    }

    if (FAILED(CreateRootSignature(&skyboxRootDesc, &m_skyboxRootSignature))) { return false; }

    ComPtr<IDxcBlob> skyboxVS;
    ComPtr<IDxcBlob> skyboxPS;

#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    skyboxVS = CompileShaderDXC(L"Contents/shaders/skybox.hlsl", L"VSMain", L"vs_6_5");
    if (!skyboxVS) return false;

    skyboxPS = CompileShaderDXC(L"Contents/shaders/skybox.hlsl", L"PSMain", L"ps_6_5");
    if (!skyboxPS) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPsoDesc = {};
    skyboxPsoDesc.pRootSignature = m_skyboxRootSignature.Get();
    skyboxPsoDesc.InputLayout = { nullptr, 0 };
    skyboxPsoDesc.VS = { skyboxVS->GetBufferPointer(), skyboxVS->GetBufferSize() };
    skyboxPsoDesc.PS = { skyboxPS->GetBufferPointer(), skyboxPS->GetBufferSize() };
    skyboxPsoDesc.RasterizerState = rasterDesc;
    skyboxPsoDesc.BlendState = blendDesc;
    skyboxPsoDesc.DepthStencilState.DepthEnable = TRUE;
    skyboxPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Skybox shouldn't write depth
    skyboxPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyboxPsoDesc.SampleMask = UINT_MAX;
    skyboxPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    skyboxPsoDesc.NumRenderTargets = 2;
    skyboxPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    skyboxPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    skyboxPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    skyboxPsoDesc.SampleDesc.Count = 1;

    if (FAILED(m_pipelineCache.Graphics("m_skyboxPipelineState",&skyboxPsoDesc,IID_PPV_ARGS(&m_skyboxPipelineState)))) { return false; }

    D3D12_RESOURCE_DESC skyboxCbDesc = {};
    skyboxCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    skyboxCbDesc.Width = (sizeof(SkyboxConstantBuffer) + 255) & ~255;
    skyboxCbDesc.Height = 1;
    skyboxCbDesc.DepthOrArraySize = 1;
    skyboxCbDesc.MipLevels = 1;
    skyboxCbDesc.Format = DXGI_FORMAT_UNKNOWN;
    skyboxCbDesc.SampleDesc.Count = 1;
    skyboxCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &skyboxCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skyboxConstantBuffer)))) { return false; }
    if (m_skyboxConstantBuffer) m_skyboxConstantBuffer->SetName(L"SkyboxConstantBuffer");

    D3D12_CONSTANT_BUFFER_VIEW_DESC skyboxCbvDesc = {};
    skyboxCbvDesc.BufferLocation = m_skyboxConstantBuffer->GetGPUVirtualAddress();
    skyboxCbvDesc.SizeInBytes = skyboxCbDesc.Width;
    
    D3D12_CPU_DESCRIPTOR_HANDLE skyboxHeapHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateConstantBufferView(&skyboxCbvDesc, skyboxHeapHandle);

    cbvReadRange = { 0, 0 };
    if (FAILED(m_skyboxConstantBuffer->Map(0, &cbvReadRange, reinterpret_cast<void**>(&m_pSkyboxCbvDataBegin)))) {
                return false;
    }

    if (m_skyboxConstantBuffer) m_skyboxConstantBuffer->SetName(L"SkyboxConstantBuffer");
    
    D3D12_DESCRIPTOR_RANGE1 ppRange0;
    ppRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ppRange0.NumDescriptors = 1;
    ppRange0.BaseShaderRegister = 0;
    ppRange0.RegisterSpace = 0;
    ppRange0.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ppRange0.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE1 ppRange1;
    ppRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ppRange1.NumDescriptors = 1;
    ppRange1.BaseShaderRegister = 1;
    ppRange1.RegisterSpace = 0;
    ppRange1.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ppRange1.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE1 ppRange2;
    ppRange2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ppRange2.NumDescriptors = 1;
    ppRange2.BaseShaderRegister = 2;
    ppRange2.RegisterSpace = 0;
    ppRange2.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ppRange2.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 ppParams[3];
    ppParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ppParams[0].DescriptorTable.NumDescriptorRanges = 1;
    ppParams[0].DescriptorTable.pDescriptorRanges = &ppRange0;
    ppParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    ppParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    ppParams[1].DescriptorTable.NumDescriptorRanges = 1;
    ppParams[1].DescriptorTable.pDescriptorRanges = &ppRange1;
    ppParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    ppParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    ppParams[2].Constants.ShaderRegister=0;ppParams[2].Constants.RegisterSpace=0;ppParams[2].Constants.Num32BitValues=32;
    ppParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC ppSampler = {};
    ppSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    ppSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ppSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ppSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ppSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    ppSampler.MaxLOD = D3D12_FLOAT32_MAX;
    ppSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC ppRootDesc = {};
    ppRootDesc.Version = m_highestRootSignatureVersion;
    if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
        ppRootDesc.Desc_1_1.NumParameters = 3;
        ppRootDesc.Desc_1_1.pParameters = ppParams;
        ppRootDesc.Desc_1_1.NumStaticSamplers = 1;
        ppRootDesc.Desc_1_1.pStaticSamplers = &ppSampler;
        ppRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    } else {
        static D3D12_ROOT_PARAMETER ppParamsV10[3];
        static D3D12_DESCRIPTOR_RANGE ppRangesV10[3][1];
        for (int i = 0; i < 3; ++i) {
            ppParamsV10[i].ParameterType = ppParams[i].ParameterType;
            ppParamsV10[i].ShaderVisibility = ppParams[i].ShaderVisibility;
            if (ppParams[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV) {
                ppParamsV10[i].Descriptor.ShaderRegister = ppParams[i].Descriptor.ShaderRegister;
                ppParamsV10[i].Descriptor.RegisterSpace = ppParams[i].Descriptor.RegisterSpace;
            } else if(ppParams[i].ParameterType==D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS){
                ppParamsV10[i].Constants=ppParams[i].Constants;
            } else {
                ppParamsV10[i].DescriptorTable.NumDescriptorRanges = ppParams[i].DescriptorTable.NumDescriptorRanges;
                ppRangesV10[i][0].RangeType = ppParams[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                ppRangesV10[i][0].NumDescriptors = ppParams[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                ppRangesV10[i][0].BaseShaderRegister = ppParams[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                ppRangesV10[i][0].RegisterSpace = ppParams[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                ppRangesV10[i][0].OffsetInDescriptorsFromTableStart = ppParams[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                ppParamsV10[i].DescriptorTable.pDescriptorRanges = ppRangesV10[i];
            }
        }
        ppRootDesc.Desc_1_0.NumParameters = 3;
        ppRootDesc.Desc_1_0.pParameters = ppParamsV10;
        ppRootDesc.Desc_1_0.NumStaticSamplers = 1;
        static D3D12_STATIC_SAMPLER_DESC ppSamplerV10;
        memcpy(&ppSamplerV10, &ppSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
        ppRootDesc.Desc_1_0.pStaticSamplers = &ppSamplerV10;
        ppRootDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    }

    if (FAILED(CreateRootSignature(&ppRootDesc, &m_postProcessRootSignature))) { return false; }

    ComPtr<IDxcBlob> ppVS, ppPS;
    ppVS = CompileShaderDXC(L"Contents/shaders/postprocess.hlsl", L"VSMain", L"vs_6_5");
    if (!ppVS) return false;
    ppPS = CompileShaderDXC(L"Contents/shaders/postprocess.hlsl", L"PSMain", L"ps_6_5");
    if (!ppPS) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC ppPsoDesc = {};
    ppPsoDesc.InputLayout = { nullptr, 0 };
    ppPsoDesc.pRootSignature = m_postProcessRootSignature.Get();
    ppPsoDesc.VS = { ppVS->GetBufferPointer(), ppVS->GetBufferSize() };
    ppPsoDesc.PS = { ppPS->GetBufferPointer(), ppPS->GetBufferSize() };
    ppPsoDesc.RasterizerState = rasterDesc;
    ppPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ppPsoDesc.BlendState = blendDesc;
    ppPsoDesc.DepthStencilState.DepthEnable = FALSE;
    ppPsoDesc.DepthStencilState.StencilEnable = FALSE;
    ppPsoDesc.SampleMask = UINT_MAX;
    ppPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ppPsoDesc.NumRenderTargets = 1;
    ppPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    ppPsoDesc.SampleDesc.Count = 1;

    if (FAILED(m_pipelineCache.Graphics("m_postProcessPipelineState",&ppPsoDesc,IID_PPV_ARGS(&m_postProcessPipelineState)))) { return false; }
    
    {
        
        ComPtr<IDxcBlob> cloudGatherVS = CompileShaderDXC(L"Contents/shaders/mboit_gather.hlsl", L"VSMain", L"vs_6_5");
        ComPtr<IDxcBlob> cloudGatherPS = CompileShaderDXC(L"Contents/shaders/mboit_gather.hlsl", L"PSMain", L"ps_6_5");
        
        if (cloudGatherVS && cloudGatherPS) {
            D3D12_DESCRIPTOR_RANGE1 rangeMoments = {};
            rangeMoments.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            rangeMoments.NumDescriptors = 1;
            rangeMoments.BaseShaderRegister = 0;
            rangeMoments.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
            
            D3D12_DESCRIPTOR_RANGE1 rangeDepth = {};
            rangeDepth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            rangeDepth.NumDescriptors = 1;
            rangeDepth.BaseShaderRegister = 1;
            rangeDepth.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            D3D12_ROOT_PARAMETER1 params[3];
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].Descriptor = { 0, 0 };
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &rangeMoments;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            
            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[2].DescriptorTable.NumDescriptorRanges = 1;
            params[2].DescriptorTable.pDescriptorRanges = &rangeDepth;
            params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                rsDesc.Desc_1_1.NumParameters = 3;
                rsDesc.Desc_1_1.pParameters = params;
                rsDesc.Desc_1_1.NumStaticSamplers = 1;
                rsDesc.Desc_1_1.pStaticSamplers = &sampler;
                rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            } else {
                static D3D12_ROOT_PARAMETER paramsV10[3];
                static D3D12_DESCRIPTOR_RANGE rangesV10[3][1];
                for(int i=0; i<3; ++i) {
                    paramsV10[i].ParameterType = params[i].ParameterType;
                    paramsV10[i].ShaderVisibility = params[i].ShaderVisibility;
                    if(params[i].ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV) {
                        paramsV10[i].Descriptor.ShaderRegister = params[i].Descriptor.ShaderRegister;
                        paramsV10[i].Descriptor.RegisterSpace = params[i].Descriptor.RegisterSpace;
                    } else {
                        paramsV10[i].DescriptorTable.NumDescriptorRanges = 1;
                        rangesV10[i][0].RangeType = params[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                        rangesV10[i][0].NumDescriptors = params[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                        rangesV10[i][0].BaseShaderRegister = params[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                        rangesV10[i][0].RegisterSpace = params[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                        rangesV10[i][0].OffsetInDescriptorsFromTableStart = params[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                        paramsV10[i].DescriptorTable.pDescriptorRanges = rangesV10[i];
                    }
                }
                rsDesc.Desc_1_0.NumParameters = 3;
                rsDesc.Desc_1_0.pParameters = paramsV10;
                rsDesc.Desc_1_0.NumStaticSamplers = 1;
                static D3D12_STATIC_SAMPLER_DESC samplerV10;
                memcpy(&samplerV10, &sampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
                rsDesc.Desc_1_0.pStaticSamplers = &samplerV10;
                rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            }

            if (FAILED(CreateRootSignature(&rsDesc, &m_cloudGatherRootSignature))) {
                LOG("Cloud Gather Root Signature creation failed");
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC gatherPsoDesc = {};
            gatherPsoDesc.pRootSignature = m_cloudGatherRootSignature.Get();
            gatherPsoDesc.VS = { cloudGatherVS->GetBufferPointer(), cloudGatherVS->GetBufferSize() };
            gatherPsoDesc.PS = { cloudGatherPS->GetBufferPointer(), cloudGatherPS->GetBufferSize() };
        
            gatherPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            gatherPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            gatherPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
            gatherPsoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            gatherPsoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            gatherPsoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            gatherPsoDesc.RasterizerState.DepthClipEnable = TRUE;
            gatherPsoDesc.RasterizerState.MultisampleEnable = FALSE;
            gatherPsoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
            gatherPsoDesc.RasterizerState.ForcedSampleCount = 0;
            gatherPsoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            gatherPsoDesc.BlendState.AlphaToCoverageEnable = FALSE;
            gatherPsoDesc.BlendState.IndependentBlendEnable = FALSE;
            for (auto& rt : gatherPsoDesc.BlendState.RenderTarget) {
                rt.BlendEnable = FALSE;
                rt.LogicOpEnable = FALSE;
                rt.SrcBlend = D3D12_BLEND_ONE;
                rt.DestBlend = D3D12_BLEND_ZERO;
                rt.BlendOp = D3D12_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D12_BLEND_ONE;
                rt.DestBlendAlpha = D3D12_BLEND_ZERO;
                rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rt.LogicOp = D3D12_LOGIC_OP_NOOP;
                rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }

            gatherPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            gatherPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            gatherPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
            gatherPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            gatherPsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            gatherPsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
            gatherPsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

            gatherPsoDesc.DepthStencilState.DepthEnable = FALSE;
            gatherPsoDesc.SampleMask = UINT_MAX;
            gatherPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            gatherPsoDesc.NumRenderTargets = 1;
            gatherPsoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
            gatherPsoDesc.SampleDesc.Count = 1;
            
            HRESULT hr = m_pipelineCache.Graphics("m_cloudGatherPipelineState",&gatherPsoDesc,IID_PPV_ARGS(&m_cloudGatherPipelineState));
            {
                if (FAILED(hr)) { LOG("Failed to create cloud gather PSO: 0x" << std::hex << hr); }
                else { LOG("Cloud gather PSO created ok"); }
            }

            ComPtr<IDxcBlob> cloudResolveVS = CompileShaderDXC(L"Contents/shaders/mboit_resolve.hlsl", L"VSMain", L"vs_6_5");
            ComPtr<IDxcBlob> cloudResolvePS = CompileShaderDXC(L"Contents/shaders/mboit_resolve.hlsl", L"PSMain", L"ps_6_5");
            if (cloudResolveVS && cloudResolvePS) {
                m_cloudResolveRootSignature = m_cloudGatherRootSignature; 
                
                D3D12_GRAPHICS_PIPELINE_STATE_DESC resolvePsoDesc = gatherPsoDesc; 
                resolvePsoDesc.pRootSignature = m_cloudResolveRootSignature.Get();
                resolvePsoDesc.VS = { cloudResolveVS->GetBufferPointer(), cloudResolveVS->GetBufferSize() };
                resolvePsoDesc.PS = { cloudResolvePS->GetBufferPointer(), cloudResolvePS->GetBufferSize() };
                
                resolvePsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                resolvePsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
                resolvePsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                resolvePsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                resolvePsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                resolvePsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                resolvePsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

                resolvePsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                
                HRESULT hr = m_pipelineCache.Graphics("m_cloudResolvePipelineState",&resolvePsoDesc,IID_PPV_ARGS(&m_cloudResolvePipelineState));
                {
                    if (FAILED(hr)) { LOG("Failed to create cloud resolve PSO"); }
                    else { LOG("Cloud resolve PSO created ok"); }
                }

            }
        }
    }

    LOG("Initializing Composite Pipeline");
    {
        // Create root signature for composite (4 SRVs: SceneColor, SSAO, SSGI, RayTracing)
        // Using version 1.0 structures (non-versioned desc)
        D3D12_ROOT_PARAMETER compParams[8];
        D3D12_DESCRIPTOR_RANGE compRanges[7];
        ZeroMemory(compParams, sizeof(compParams));
        ZeroMemory(compRanges, sizeof(compRanges));
        // CBV at b0
        compParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        compParams[0].Descriptor.ShaderRegister = 0;
        compParams[0].Descriptor.RegisterSpace = 0;
        compParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        // SRV descriptor tables at t0-t3
        for (int i = 0; i < 7; ++i) {
            compRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            compRanges[i].NumDescriptors = 1;
            compRanges[i].BaseShaderRegister = i; // t0, t1, t2, t3
            compRanges[i].RegisterSpace = 0;
            compRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            compParams[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            compParams[i + 1].DescriptorTable.NumDescriptorRanges = 1;
            compParams[i + 1].DescriptorTable.pDescriptorRanges = &compRanges[i];
            compParams[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_STATIC_SAMPLER_DESC compSampler;
        ZeroMemory(&compSampler, sizeof(compSampler));
        compSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        compSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        compSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        compSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        compSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        compSampler.MaxLOD = D3D12_FLOAT32_MAX;
        compSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        
        D3D12_ROOT_SIGNATURE_DESC compRootDesc = {};
        compRootDesc.NumParameters = 8;
        compRootDesc.pParameters = compParams;
        compRootDesc.NumStaticSamplers = 1;
        compRootDesc.pStaticSamplers = &compSampler;
        compRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        
        ComPtr<ID3DBlob> compRSBlob, compRSError;
        HRESULT hr = D3D12SerializeRootSignature(&compRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &compRSBlob, &compRSError);
        if (FAILED(hr)) {
            LOG("ERROR: Failed to serialize composite root signature!");
            if (compRSError) LOG((char*)compRSError->GetBufferPointer());
        } else {
            hr = m_device->CreateRootSignature(0, compRSBlob->GetBufferPointer(), compRSBlob->GetBufferSize(), IID_PPV_ARGS(&m_compositeRootSignature));
            if(SUCCEEDED(hr))m_pipelineCache.RegisterRoot(m_compositeRootSignature.Get(),compRSBlob->GetBufferPointer(),compRSBlob->GetBufferSize());
            if (FAILED(hr)) {
                LOG("ERROR: Failed to create composite root signature! HRESULT=" << std::hex << hr);
            } else {
                LOG("Composite root signature created");
            }
        }

        ComPtr<IDxcBlob> compVS = CompileShaderDXC(L"Contents/shaders/composite.hlsl", L"VSMain", L"vs_6_5");
        ComPtr<IDxcBlob> compPS = CompileShaderDXC(L"Contents/shaders/composite.hlsl", L"PSMain", L"ps_6_5");
        if (compVS && compPS && m_compositeRootSignature) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC compPsoDesc = ppPsoDesc;
            compPsoDesc.InputLayout = { nullptr, 0 };
            compPsoDesc.pRootSignature = m_compositeRootSignature.Get();
            compPsoDesc.VS = { compVS->GetBufferPointer(), compVS->GetBufferSize() };
            compPsoDesc.PS = { compPS->GetBufferPointer(), compPS->GetBufferSize() };
            compPsoDesc.NumRenderTargets = 1;
            compPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            
            HRESULT hr = m_pipelineCache.Graphics("m_compositePipelineState",&compPsoDesc,IID_PPV_ARGS(&m_compositePipelineState));
            if (FAILED(hr)) {
                LOG("ERROR: Failed to create composite PSO! HRESULT=" << std::hex << hr);
            } else {
                LOG("Composite PSO created successfully");
            }
        } else {
            LOG("ERROR: Composite setup failed - VS=" << (void*)compVS.Get() << " PS=" << (void*)compPS.Get() << " RS=" << (void*)m_compositeRootSignature.Get());
        }
    }

    LOG("Initializing SSAO Pipeline");
    {
        srand(42);
        SSAOConstants ssaoInitData = {};
        for (int i = 0; i < 16; i++) {
            float x = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            float y = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
            float z = (float)rand() / RAND_MAX;
            XMVECTOR sample = XMVector3Normalize(XMVectorSet(x, y, z, 0.0f));
            float scale = (float)i / 16.0f;
            scale = 0.1f + scale * scale * 0.9f;
            sample = XMVectorScale(sample, scale);
            XMStoreFloat4(&ssaoInitData.samples[i], sample);
        }

        D3D12_DESCRIPTOR_RANGE1 ssaoRange = {};
        ssaoRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ssaoRange.NumDescriptors = 1;
        ssaoRange.BaseShaderRegister = 0;
        ssaoRange.RegisterSpace = 0;
        ssaoRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        ssaoRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 ssaoParams[2];
        ssaoParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ssaoParams[0].Descriptor = { 0, 0 }; // b0
        ssaoParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        ssaoParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ssaoParams[1].DescriptorTable.NumDescriptorRanges = 1;
        ssaoParams[1].DescriptorTable.pDescriptorRanges = &ssaoRange;
        ssaoParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ssaoSampler = {};
        ssaoSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        ssaoSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssaoSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssaoSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssaoSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ssaoSampler.MaxLOD = D3D12_FLOAT32_MAX;
        ssaoSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC ssaoRootDesc = {};
        ssaoRootDesc.Version = m_highestRootSignatureVersion;
        if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
            ssaoRootDesc.Desc_1_1.NumParameters = 2;
            ssaoRootDesc.Desc_1_1.pParameters = ssaoParams;
            ssaoRootDesc.Desc_1_1.NumStaticSamplers = 1;
            ssaoRootDesc.Desc_1_1.pStaticSamplers = &ssaoSampler;
            ssaoRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        } else {
            static D3D12_ROOT_PARAMETER ssaoParamsV10[2];
            static D3D12_DESCRIPTOR_RANGE ssaoRangeV10[1];
            
            ssaoParamsV10[0].ParameterType = ssaoParams[0].ParameterType;
            ssaoParamsV10[0].Descriptor.ShaderRegister = ssaoParams[0].Descriptor.ShaderRegister;
            ssaoParamsV10[0].Descriptor.RegisterSpace = ssaoParams[0].Descriptor.RegisterSpace;
            ssaoParamsV10[0].ShaderVisibility = ssaoParams[0].ShaderVisibility;

            ssaoRangeV10[0].RangeType = ssaoRange.RangeType;
            ssaoRangeV10[0].NumDescriptors = ssaoRange.NumDescriptors;
            ssaoRangeV10[0].BaseShaderRegister = ssaoRange.BaseShaderRegister;
            ssaoRangeV10[0].RegisterSpace = ssaoRange.RegisterSpace;
            ssaoRangeV10[0].OffsetInDescriptorsFromTableStart = ssaoRange.OffsetInDescriptorsFromTableStart;

            ssaoParamsV10[1].ParameterType = ssaoParams[1].ParameterType;
            ssaoParamsV10[1].DescriptorTable.NumDescriptorRanges = 1;
            ssaoParamsV10[1].DescriptorTable.pDescriptorRanges = ssaoRangeV10;
            ssaoParamsV10[1].ShaderVisibility = ssaoParams[1].ShaderVisibility;

            ssaoRootDesc.Desc_1_0.NumParameters = 2;
            ssaoRootDesc.Desc_1_0.pParameters = ssaoParamsV10;
            ssaoRootDesc.Desc_1_0.NumStaticSamplers = 1;
            static D3D12_STATIC_SAMPLER_DESC ssaoSamplerV10;
            memcpy(&ssaoSamplerV10, &ssaoSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
            ssaoRootDesc.Desc_1_0.pStaticSamplers = &ssaoSamplerV10;
            ssaoRootDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        }

        if (FAILED(CreateRootSignature(&ssaoRootDesc, &m_ssaoRootSignature))) { return false; }

        D3D12_DESCRIPTOR_RANGE1 blurRange = {};
        blurRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        blurRange.NumDescriptors = 2;
        blurRange.BaseShaderRegister = 0;
        blurRange.RegisterSpace = 0;
        blurRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        blurRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 blurParams[2];
        blurParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        blurParams[0].Descriptor = { 0, 0 };
        blurParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        blurParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        blurParams[1].DescriptorTable.NumDescriptorRanges = 1;
        blurParams[1].DescriptorTable.pDescriptorRanges = &blurRange;
        blurParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC blurRootDesc = {};
        blurRootDesc.Version = m_highestRootSignatureVersion;
        if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
            blurRootDesc.Desc_1_1.NumParameters = 2;
            blurRootDesc.Desc_1_1.pParameters = blurParams;
            blurRootDesc.Desc_1_1.NumStaticSamplers = 1;
            blurRootDesc.Desc_1_1.pStaticSamplers = &ssaoSampler;
            blurRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        } else {
            static D3D12_ROOT_PARAMETER blurParamsV10[2];
            static D3D12_DESCRIPTOR_RANGE blurRangeV10[1];

            blurParamsV10[0].ParameterType = blurParams[0].ParameterType;
            blurParamsV10[0].Descriptor.ShaderRegister = blurParams[0].Descriptor.ShaderRegister;
            blurParamsV10[0].Descriptor.RegisterSpace = blurParams[0].Descriptor.RegisterSpace;
            blurParamsV10[0].ShaderVisibility = blurParams[0].ShaderVisibility;

            blurRangeV10[0].RangeType = blurRange.RangeType;
            blurRangeV10[0].NumDescriptors = blurRange.NumDescriptors;
            blurRangeV10[0].BaseShaderRegister = blurRange.BaseShaderRegister;
            blurRangeV10[0].RegisterSpace = blurRange.RegisterSpace;
            blurRangeV10[0].OffsetInDescriptorsFromTableStart = blurRange.OffsetInDescriptorsFromTableStart;

            blurParamsV10[1].ParameterType = blurParams[1].ParameterType;
            blurParamsV10[1].DescriptorTable.NumDescriptorRanges = 1;
            blurParamsV10[1].DescriptorTable.pDescriptorRanges = blurRangeV10;
            blurParamsV10[1].ShaderVisibility = blurParams[1].ShaderVisibility;

            blurRootDesc.Desc_1_0.NumParameters = 2;
            blurRootDesc.Desc_1_0.pParameters = blurParamsV10;
            blurRootDesc.Desc_1_0.NumStaticSamplers = 1;
            static D3D12_STATIC_SAMPLER_DESC blurSamplerV10;
            memcpy(&blurSamplerV10, &ssaoSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
            blurRootDesc.Desc_1_0.pStaticSamplers = &blurSamplerV10;
            blurRootDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        }

        if (FAILED(CreateRootSignature(&blurRootDesc, &m_ssaoBlurRootSignature))) { return false; }

        D3D12_DESCRIPTOR_RANGE1 ssgiSrvRange = {};
        ssgiSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ssgiSrvRange.NumDescriptors = 4;
        ssgiSrvRange.BaseShaderRegister = 0;
        ssgiSrvRange.RegisterSpace = 0;
        ssgiSrvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

        D3D12_DESCRIPTOR_RANGE1 ssgiUavRange = {};
        ssgiUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ssgiUavRange.NumDescriptors = 1;
        ssgiUavRange.BaseShaderRegister = 0;
        ssgiUavRange.RegisterSpace = 0;
        ssgiUavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

        D3D12_ROOT_PARAMETER1 ssgiParams[3] = {};
        ssgiParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ssgiParams[0].DescriptorTable.NumDescriptorRanges = 1;
        ssgiParams[0].DescriptorTable.pDescriptorRanges = &ssgiSrvRange;
        ssgiParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        ssgiParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ssgiParams[1].DescriptorTable.NumDescriptorRanges = 1;
        ssgiParams[1].DescriptorTable.pDescriptorRanges = &ssgiUavRange;
        ssgiParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        ssgiParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ssgiParams[2].Descriptor.ShaderRegister = 0;
        ssgiParams[2].Descriptor.RegisterSpace = 0;
        ssgiParams[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
        ssgiParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC ssgiSampler = {};
        ssgiSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // Use point for depth, linear for color if needed, point is safer here.
        ssgiSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssgiSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssgiSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ssgiSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ssgiSampler.MaxLOD = D3D12_FLOAT32_MAX;
        ssgiSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC ssgiRootDesc = {};
        ssgiRootDesc.Version = m_highestRootSignatureVersion;
        if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
            ssgiRootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            ssgiRootDesc.Desc_1_1.NumParameters = 3;
            ssgiRootDesc.Desc_1_1.pParameters = ssgiParams;
            ssgiRootDesc.Desc_1_1.NumStaticSamplers = 1;
            ssgiRootDesc.Desc_1_1.pStaticSamplers = &ssgiSampler;
            ssgiRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        } else {
            static D3D12_ROOT_PARAMETER ssgiParamsV10[3];
            static D3D12_DESCRIPTOR_RANGE ssgiRangesV10[2][1];
            
            for (int i = 0; i < 2; ++i) {
                ssgiParamsV10[i].ParameterType = ssgiParams[i].ParameterType;
                ssgiParamsV10[i].DescriptorTable.NumDescriptorRanges = 1;
                
                ssgiRangesV10[i][0].RangeType = ssgiParams[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                ssgiRangesV10[i][0].NumDescriptors = ssgiParams[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                ssgiRangesV10[i][0].BaseShaderRegister = ssgiParams[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                ssgiRangesV10[i][0].RegisterSpace = ssgiParams[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                ssgiRangesV10[i][0].OffsetInDescriptorsFromTableStart = ssgiParams[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                
                ssgiParamsV10[i].DescriptorTable.pDescriptorRanges = ssgiRangesV10[i];
                ssgiParamsV10[i].ShaderVisibility = ssgiParams[i].ShaderVisibility;
            }
            
            ssgiParamsV10[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            ssgiParamsV10[2].Descriptor.ShaderRegister = ssgiParams[2].Descriptor.ShaderRegister;
            ssgiParamsV10[2].Descriptor.RegisterSpace = ssgiParams[2].Descriptor.RegisterSpace;
            ssgiParamsV10[2].ShaderVisibility = ssgiParams[2].ShaderVisibility;

            ssgiRootDesc.Desc_1_0.NumParameters = 3;
            ssgiRootDesc.Desc_1_0.pParameters = ssgiParamsV10;
            ssgiRootDesc.Desc_1_0.NumStaticSamplers = 1;
            static D3D12_STATIC_SAMPLER_DESC ssgiSamplerV10;
            memcpy(&ssgiSamplerV10, &ssgiSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
            ssgiRootDesc.Desc_1_0.pStaticSamplers = &ssgiSamplerV10;
            ssgiRootDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        }

        if (FAILED(CreateRootSignature(&ssgiRootDesc, &m_ssgiRootSignature))) { return false; }

        ComPtr<IDxcBlob> ssgiCS = CompileShaderDXC(L"Contents/shaders/ssgi.hlsl", L"CSMain", L"cs_6_0");
        if (ssgiCS && m_ssgiRootSignature) {
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = m_ssgiRootSignature.Get();
            psoDesc.CS = { ssgiCS->GetBufferPointer(), ssgiCS->GetBufferSize() };
            if (SUCCEEDED(m_pipelineCache.Compute("m_ssgiPipelineState",&psoDesc,IID_PPV_ARGS(&m_ssgiPipelineState)))) {
                LOG("SSGI: Compute Pipeline State created");
            }
        }

        LOG("SSAO: Compiling VS");
        ComPtr<IDxcBlob> ssaoVS, ssaoPS;
        ssaoVS = CompileShaderDXC(L"Contents/shaders/ssao.hlsl", L"VSMain", L"vs_6_0");
        if (!ssaoVS) return false;
        LOG("SSAO: Compiling PS");
        ssaoPS = CompileShaderDXC(L"Contents/shaders/ssao.hlsl", L"PSMain", L"ps_6_0");
        if (!ssaoPS) return false;

        LOG("SSAO: Creating PSO");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = ppPsoDesc;
        ssaoPsoDesc.pRootSignature = m_ssaoRootSignature.Get();
        ssaoPsoDesc.VS = { ssaoVS->GetBufferPointer(), ssaoVS->GetBufferSize() };
        ssaoPsoDesc.PS = { ssaoPS->GetBufferPointer(), ssaoPS->GetBufferSize() };
        ssaoPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
            if (FAILED(m_pipelineCache.Graphics("m_ssaoPipelineState",&ssaoPsoDesc,IID_PPV_ARGS(&m_ssaoPipelineState)))) {
                LOG("SSAO: PSO Creation FAILED");
                return false;
    }
        LOG("SSAO: PSO Created");
    
        LOG("SSAO: Compiling Blur VS");
        ComPtr<IDxcBlob> blurVS, blurPS;
        blurVS = CompileShaderDXC(L"Contents/shaders/ssao_blur.hlsl", L"VSMain", L"vs_6_0");
        if (!blurVS) return false;
        LOG("SSAO: Compiling Blur PS");
        blurPS = CompileShaderDXC(L"Contents/shaders/ssao_blur.hlsl", L"PSMain", L"ps_6_0");
        if (!blurPS) return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC blurPsoDesc = ppPsoDesc;
        blurPsoDesc.pRootSignature = m_ssaoBlurRootSignature.Get();
        blurPsoDesc.VS = { blurVS->GetBufferPointer(), blurVS->GetBufferSize() };
        blurPsoDesc.PS = { blurPS->GetBufferPointer(), blurPS->GetBufferSize() };
        blurPsoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        LOG("SSAO: Creating Blur PSO");
        if (FAILED(m_pipelineCache.Graphics("m_ssaoBlurPipelineState",&blurPsoDesc,IID_PPV_ARGS(&m_ssaoBlurPipelineState)))) {
            LOG("SSAO: Blur PSO creation failed");
            return false;
        }
        LOG("SSAO: Blur PSO Created");

        LOG("SSAO: Creating CBs");

        // Hi-Z Compute Pipeline
        {
            D3D12_DESCRIPTOR_RANGE1 srvRange = {};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 1;
            srvRange.BaseShaderRegister = 0;

            D3D12_DESCRIPTOR_RANGE1 uavRange = {};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0;

            D3D12_ROOT_PARAMETER1 params[3] = {};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[0].DescriptorTable.NumDescriptorRanges = 1;
            params[0].DescriptorTable.pDescriptorRanges = &srvRange;
            
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &uavRange;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[2].Constants.ShaderRegister = 0;
            params[2].Constants.Num32BitValues = 2;

            D3D12_STATIC_SAMPLER_DESC hizSampler = {};
            hizSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            hizSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            hizSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            hizSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            hizSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            hizSampler.MaxLOD = D3D12_FLOAT32_MAX;
            hizSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                rsDesc.Desc_1_1.NumParameters = 3;
                rsDesc.Desc_1_1.pParameters = params;
                rsDesc.Desc_1_1.NumStaticSamplers = 1;
                rsDesc.Desc_1_1.pStaticSamplers = &hizSampler;
                rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            } else {
                static D3D12_ROOT_PARAMETER paramsV10[3];
                static D3D12_DESCRIPTOR_RANGE rangesV10[2][1];
                for(int i=0; i<2; ++i) {
                    paramsV10[i].ParameterType = params[i].ParameterType;
                    paramsV10[i].DescriptorTable.NumDescriptorRanges = 1;
                    rangesV10[i][0].RangeType = params[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                    rangesV10[i][0].NumDescriptors = params[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                    rangesV10[i][0].BaseShaderRegister = params[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                    rangesV10[i][0].RegisterSpace = params[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                    rangesV10[i][0].OffsetInDescriptorsFromTableStart = params[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                    paramsV10[i].DescriptorTable.pDescriptorRanges = rangesV10[i];
                    paramsV10[i].ShaderVisibility = params[i].ShaderVisibility;
                }
                paramsV10[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                paramsV10[2].Constants.ShaderRegister = params[2].Constants.ShaderRegister;
                paramsV10[2].Constants.RegisterSpace = params[2].Constants.RegisterSpace;
                paramsV10[2].Constants.Num32BitValues = params[2].Constants.Num32BitValues;
                paramsV10[2].ShaderVisibility = params[2].ShaderVisibility;

                static D3D12_STATIC_SAMPLER_DESC hizSamplerV10;
                memcpy(&hizSamplerV10, &hizSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));

                rsDesc.Desc_1_0.NumParameters = 3;
                rsDesc.Desc_1_0.pParameters = paramsV10;
                rsDesc.Desc_1_0.NumStaticSamplers = 1;
                rsDesc.Desc_1_0.pStaticSamplers = &hizSamplerV10;
                rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            }

            if (FAILED(CreateRootSignature(&rsDesc, &m_hizRootSignature))) {
                LOG("Hi-Z Root Signature creation failed");
            }

            ComPtr<IDxcBlob> hizCS = CompileShaderDXC(L"Contents/shaders/hiz.hlsl", L"CSMain", L"cs_6_5");
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = m_hizRootSignature.Get();
            psoDesc.CS = { hizCS->GetBufferPointer(), hizCS->GetBufferSize() };
            m_pipelineCache.Compute("m_hizPipelineState",&psoDesc,IID_PPV_ARGS(&m_hizPipelineState));
        }

        // VSM Management Compute Pipeline
        {
            D3D12_DESCRIPTOR_RANGE1 srvRange = {};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 1; // Main Depth
            srvRange.BaseShaderRegister = 0;

            D3D12_DESCRIPTOR_RANGE1 uavRange = {};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 2; // Page Table, Atlas
            uavRange.BaseShaderRegister = 0;

            D3D12_ROOT_PARAMETER1 params[3] = {};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[0].DescriptorTable.NumDescriptorRanges = 1;
            params[0].DescriptorTable.pDescriptorRanges = &srvRange;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &uavRange;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[2].Constants.ShaderRegister = 0;
            params[2].Constants.Num32BitValues = 20; // 4 (ScreenRes + AtlasRes pack) + 16 (SunMVP) = 20 floats

            D3D12_STATIC_SAMPLER_DESC vsmSampler = {};
            vsmSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            vsmSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            vsmSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            vsmSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            vsmSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            vsmSampler.MaxLOD = D3D12_FLOAT32_MAX;
            vsmSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                rsDesc.Desc_1_1.NumParameters = 3;
                rsDesc.Desc_1_1.pParameters = params;
                rsDesc.Desc_1_1.NumStaticSamplers = 1;
                rsDesc.Desc_1_1.pStaticSamplers = &vsmSampler;
                rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            } else {
                static D3D12_ROOT_PARAMETER paramsV10[3];
                static D3D12_DESCRIPTOR_RANGE rangesV10[2][1];
                for (int i = 0; i < 2; ++i) {
                    paramsV10[i].ParameterType = params[i].ParameterType;
                    paramsV10[i].DescriptorTable.NumDescriptorRanges = 1;
                    rangesV10[i][0].RangeType = params[i].DescriptorTable.pDescriptorRanges[0].RangeType;
                    rangesV10[i][0].NumDescriptors = params[i].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                    rangesV10[i][0].BaseShaderRegister = params[i].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                    rangesV10[i][0].RegisterSpace = params[i].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                    rangesV10[i][0].OffsetInDescriptorsFromTableStart = params[i].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                    paramsV10[i].DescriptorTable.pDescriptorRanges = rangesV10[i];
                    paramsV10[i].ShaderVisibility = params[i].ShaderVisibility;
                }
                paramsV10[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                paramsV10[2].Constants.ShaderRegister = params[2].Constants.ShaderRegister;
                paramsV10[2].Constants.RegisterSpace = params[2].Constants.RegisterSpace;
                paramsV10[2].Constants.Num32BitValues = params[2].Constants.Num32BitValues;
                paramsV10[2].ShaderVisibility = params[2].ShaderVisibility;

                static D3D12_STATIC_SAMPLER_DESC vsmSamplerV10;
                memcpy(&vsmSamplerV10, &vsmSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));

                rsDesc.Desc_1_0.NumParameters = 3;
                rsDesc.Desc_1_0.pParameters = paramsV10;
                rsDesc.Desc_1_0.NumStaticSamplers = 1;
                rsDesc.Desc_1_0.pStaticSamplers = &vsmSamplerV10;
                rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            }

            if (FAILED(CreateRootSignature(&rsDesc, &m_vsmManagementRootSignature))) {
                LOG("VSM Management Root Signature creation failed");
            }

            ComPtr<IDxcBlob> vsmCS = CompileShaderDXC(L"Contents/shaders/vsm_management.hlsl", L"CSMain", L"cs_6_5");
            if (vsmCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_vsmManagementRootSignature.Get();
                psoDesc.CS = { vsmCS->GetBufferPointer(), vsmCS->GetBufferSize() };
                HRESULT hrPSO = m_pipelineCache.Compute("m_vsmManagementPipelineState",&psoDesc,IID_PPV_ARGS(&m_vsmManagementPipelineState));
                if (FAILED(hrPSO)) {
                    LOG("VSM Management PSO creation failed: 0x" << std::hex << hrPSO);
                } else {
                    LOG("VSM Management PSO created ok");
                }
            } else {
                LOG("VSM Management shader compilation failed");
            }
        }

        // RVT Bake Pipeline
        {
            LOG("RVT Bake: Creating Root Signature");

            D3D12_DESCRIPTOR_RANGE1 texRange = {};
            texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            texRange.NumDescriptors = 16;
            texRange.BaseShaderRegister = 0;

            D3D12_ROOT_PARAMETER1 params[3] = {};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = 8;

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &texRange;

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[2].Descriptor.ShaderRegister = 16;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                rsDesc.Desc_1_1.NumParameters = 3;
                rsDesc.Desc_1_1.pParameters = params;
                rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            } else {
                static D3D12_ROOT_PARAMETER paramsV10[3];
                static D3D12_DESCRIPTOR_RANGE rangesV10[1][1];

                paramsV10[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                paramsV10[0].Constants.ShaderRegister = params[0].Constants.ShaderRegister;
                paramsV10[0].Constants.RegisterSpace = params[0].Constants.RegisterSpace;
                paramsV10[0].Constants.Num32BitValues = params[0].Constants.Num32BitValues;
                paramsV10[0].ShaderVisibility = params[0].ShaderVisibility;

                paramsV10[1].ParameterType = params[1].ParameterType;
                paramsV10[1].DescriptorTable.NumDescriptorRanges = 1;
                rangesV10[0][0].RangeType = params[1].DescriptorTable.pDescriptorRanges[0].RangeType;
                rangesV10[0][0].NumDescriptors = params[1].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                rangesV10[0][0].BaseShaderRegister = params[1].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                rangesV10[0][0].RegisterSpace = params[1].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                rangesV10[0][0].OffsetInDescriptorsFromTableStart = params[1].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                paramsV10[1].DescriptorTable.pDescriptorRanges = rangesV10[0];
                paramsV10[1].ShaderVisibility = params[1].ShaderVisibility;

                paramsV10[2].ParameterType = params[2].ParameterType;
                paramsV10[2].Descriptor.ShaderRegister = params[2].Descriptor.ShaderRegister;
                paramsV10[2].Descriptor.RegisterSpace = params[2].Descriptor.RegisterSpace;
                paramsV10[2].ShaderVisibility = params[2].ShaderVisibility;

                rsDesc.Desc_1_0.NumParameters = 3;
                rsDesc.Desc_1_0.pParameters = paramsV10;
                rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            }

            ComPtr<ID3DBlob> errorBlob;
            HRESULT hrRS = CreateRootSignature(&rsDesc, &m_rvtRootSignature);
            if (FAILED(hrRS)) {
                LOG("RVT Bake Root Signature creation failed: 0x" << std::hex << hrRS);
                if (errorBlob) {
                    LOG("Error: " << (char*)errorBlob->GetBufferPointer());
                }
            } else {
                LOG("RVT Bake Root Signature created");
            }

            LOG("RVT Bake: Compiling VS");
            ComPtr<IDxcBlob> rvtVS = CompileShaderDXC(L"Contents/shaders/rvt.hlsl", L"VSMain", L"vs_6_5");
            if (!rvtVS) {
                LOG("RVT Bake VS compilation failed");
            } else {
                LOG("RVT Bake VS compiled");
            }

            LOG("RVT Bake: Compiling PS");
            ComPtr<IDxcBlob> rvtPS = CompileShaderDXC(L"Contents/shaders/rvt.hlsl", L"PSMain", L"ps_6_5");
            if (!rvtPS) {
                LOG("RVT Bake PS compilation failed");
            } else {
                LOG("RVT Bake PS compiled");
            }

            if (rvtVS && rvtPS && m_rvtRootSignature) {
                LOG("RVT Bake: Creating Root Signature");

                    // 1. Создаем корневую сигнатуру специально под rvt.hlsl
                    CD3DX12_ROOT_PARAMETER1 rootParameters[2];
                    
                    // Параметр 0: Constant Buffer (b0) для BakeConstants
                    rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_ALL);
                    
                    // Параметр 1: Дескрипторная таблица для 16 текстур (t0)
                    CD3DX12_DESCRIPTOR_RANGE1 texRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 16, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
                    rootParameters[1].InitAsDescriptorTable(1, &texRange, D3D12_SHADER_VISIBILITY_PIXEL);

                    // Статический сэмплер (s0) прямо в сигнатуре
                    CD3DX12_STATIC_SAMPLER_DESC staticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

                    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc;
                    rootSigDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &staticSampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

                    ComPtr<ID3DBlob> signatureBlob;
                    ComPtr<ID3DBlob> errorBlob;
                    
                    // ИСПРАВЛЕНИЕ: Используем стандартную функцию API D3D12 вместо d3dx12-хелпера
                    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signatureBlob, &errorBlob);
                    if (FAILED(hr)) {
                        if (errorBlob) {
                            LOG("Root Sig Serialization Error: " << (const char*)errorBlob->GetBufferPointer());
                        }
                        return false;
                    }

                    m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rvtBakeRootSignature));
                    LOG("RVT Bake Root Signature created");
            }
        }
        // --- Astra Radiosity Pipeline ---
        {
            D3D12_DESCRIPTOR_RANGE1 uavRange = {};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0; // u0: RVT Irradiance

            D3D12_DESCRIPTOR_RANGE1 srvRange = {};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 2; // t1: RVT Albedo, t2: RVT Normal
            srvRange.BaseShaderRegister = 1;
            srvRange.RegisterSpace = 0;
            srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            D3D12_ROOT_PARAMETER1 params[5] = {};
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[0].Descriptor = { 0, 0 }; // b0: Scene Constants

            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[1].Descriptor = { 0, 0 }; // t0: TLAS (buffer, root descriptor OK)

            params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[2].DescriptorTable.NumDescriptorRanges = 1;
            params[2].DescriptorTable.pDescriptorRanges = &srvRange; // t1, t2: RVT textures

            params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[3].DescriptorTable.NumDescriptorRanges = 1;
            params[3].DescriptorTable.pDescriptorRanges = &uavRange; // u0: RVT Irradiance

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                rsDesc.Desc_1_1.NumParameters = 4;
                rsDesc.Desc_1_1.pParameters = params;
                rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            } else {
                static D3D12_ROOT_PARAMETER paramsV10[4];
                static D3D12_DESCRIPTOR_RANGE rangesV10[2][1];
                // params[0]: CBV
                paramsV10[0].ParameterType = params[0].ParameterType;
                paramsV10[0].Descriptor.ShaderRegister = params[0].Descriptor.ShaderRegister;
                paramsV10[0].Descriptor.RegisterSpace = params[0].Descriptor.RegisterSpace;
                paramsV10[0].ShaderVisibility = params[0].ShaderVisibility;
                // params[1]: SRV (TLAS)
                paramsV10[1].ParameterType = params[1].ParameterType;
                paramsV10[1].Descriptor.ShaderRegister = params[1].Descriptor.ShaderRegister;
                paramsV10[1].Descriptor.RegisterSpace = params[1].Descriptor.RegisterSpace;
                paramsV10[1].ShaderVisibility = params[1].ShaderVisibility;
                // params[2]: SRV descriptor table
                paramsV10[2].ParameterType = params[2].ParameterType;
                paramsV10[2].DescriptorTable.NumDescriptorRanges = 1;
                rangesV10[0][0].RangeType = params[2].DescriptorTable.pDescriptorRanges[0].RangeType;
                rangesV10[0][0].NumDescriptors = params[2].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                rangesV10[0][0].BaseShaderRegister = params[2].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                rangesV10[0][0].RegisterSpace = params[2].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                rangesV10[0][0].OffsetInDescriptorsFromTableStart = params[2].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                paramsV10[2].DescriptorTable.pDescriptorRanges = rangesV10[0];
                paramsV10[2].ShaderVisibility = params[2].ShaderVisibility;
                // params[3]: UAV descriptor table
                paramsV10[3].ParameterType = params[3].ParameterType;
                paramsV10[3].DescriptorTable.NumDescriptorRanges = 1;
                rangesV10[1][0].RangeType = params[3].DescriptorTable.pDescriptorRanges[0].RangeType;
                rangesV10[1][0].NumDescriptors = params[3].DescriptorTable.pDescriptorRanges[0].NumDescriptors;
                rangesV10[1][0].BaseShaderRegister = params[3].DescriptorTable.pDescriptorRanges[0].BaseShaderRegister;
                rangesV10[1][0].RegisterSpace = params[3].DescriptorTable.pDescriptorRanges[0].RegisterSpace;
                rangesV10[1][0].OffsetInDescriptorsFromTableStart = params[3].DescriptorTable.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart;
                paramsV10[3].DescriptorTable.pDescriptorRanges = rangesV10[1];
                paramsV10[3].ShaderVisibility = params[3].ShaderVisibility;

                rsDesc.Desc_1_0.NumParameters = 4;
                rsDesc.Desc_1_0.pParameters = paramsV10;
                rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            }

            if (FAILED(CreateRootSignature(&rsDesc, &m_astraRadiosityRootSignature))) {
                LOG("Astra Radiosity Root Signature creation failed");
            }

            ComPtr<IDxcBlob> astraCS = CompileShaderDXC(L"shaders/astra_radiosity.hlsl", L"CSMain", L"cs_6_5");
            if (astraCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_astraRadiosityRootSignature.Get();
                psoDesc.CS = { astraCS->GetBufferPointer(), astraCS->GetBufferSize() };
                m_pipelineCache.Compute("m_astraRadiosityPipelineState",&psoDesc,IID_PPV_ARGS(&m_astraRadiosityPipelineState));
            }
        }

        // --- TAA Compute Pipeline ---
        {
            CD3DX12_DESCRIPTOR_RANGE1 srvRange0;
            srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0: Scene Color
            srvRange0.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            CD3DX12_DESCRIPTOR_RANGE1 srvRange1;
            srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1: Motion Vectors
            srvRange1.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            CD3DX12_DESCRIPTOR_RANGE1 srvRange2;
            srvRange2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2: History Read (for bilinear reprojection)
            srvRange2.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            CD3DX12_DESCRIPTOR_RANGE1 uavRange;
            uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0: History Buffer Write

            CD3DX12_STATIC_SAMPLER_DESC linearSampler(0); // s0: Linear sampler for history reprojection
            linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

            CD3DX12_ROOT_PARAMETER1 rootParams[5];
            rootParams[0].InitAsConstantBufferView(0);           // b0: Scene Constants
            rootParams[1].InitAsDescriptorTable(1, &srvRange0); // t0: Scene Color
            rootParams[2].InitAsDescriptorTable(1, &srvRange1); // t1: Motion Vectors
            rootParams[3].InitAsDescriptorTable(1, &uavRange);  // u0: History Write
            rootParams[4].InitAsDescriptorTable(1, &srvRange2); // t2: History Read

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                sigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &linearSampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            } else {
                static CD3DX12_ROOT_PARAMETER rootParamsV10[5];
                rootParamsV10[0].InitAsConstantBufferView(0);
                static CD3DX12_DESCRIPTOR_RANGE srvRange0V10;
                srvRange0V10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
                rootParamsV10[1].InitAsDescriptorTable(1, &srvRange0V10);
                static CD3DX12_DESCRIPTOR_RANGE srvRange1V10;
                srvRange1V10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
                rootParamsV10[2].InitAsDescriptorTable(1, &srvRange1V10);
                static CD3DX12_DESCRIPTOR_RANGE uavRangeV10;
                uavRangeV10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
                rootParamsV10[3].InitAsDescriptorTable(1, &uavRangeV10);
                static CD3DX12_DESCRIPTOR_RANGE srvRange2V10;
                srvRange2V10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
                rootParamsV10[4].InitAsDescriptorTable(1, &srvRange2V10);
                static D3D12_STATIC_SAMPLER_DESC linearSamplerV10 = {};
                linearSamplerV10.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                linearSamplerV10.AddressU = linearSamplerV10.AddressV = linearSamplerV10.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                linearSamplerV10.ShaderRegister = 0;
                linearSamplerV10.RegisterSpace = 0;
                linearSamplerV10.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                sigDesc.Init_1_0(5, rootParamsV10, 1, &linearSamplerV10, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            }

            if (FAILED(CreateRootSignature(&sigDesc, &m_taaRootSignature))) {
                LOG("TAA Root Signature creation failed");
            }

            ComPtr<IDxcBlob> taaCS = CompileShaderDXC(L"shaders/taa.hlsl", L"CSMain", L"cs_6_5");
            if (taaCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_taaRootSignature.Get();
                psoDesc.CS = { taaCS->GetBufferPointer(), taaCS->GetBufferSize() };
                m_pipelineCache.Compute("m_taaPipelineState",&psoDesc,IID_PPV_ARGS(&m_taaPipelineState));
            }
        }

        // --- FSR EASU + RCAS Fullscreen Graphics Pipeline ---
        {
            D3D12_DESCRIPTOR_RANGE1 srvRange = {};
            srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors = 1;
            srvRange.BaseShaderRegister = 0; // t0: input color
            srvRange.RegisterSpace = 0;
            srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            D3D12_ROOT_PARAMETER1 rootParams[2] = {};
            // Param 0: CBV (b0) for FSRConstants
            rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            rootParams[0].Descriptor.ShaderRegister = 0;
            rootParams[0].Descriptor.RegisterSpace = 0;
            rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            // Param 1: SRV (t0)
            rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
            rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC linearSampler = {};
            linearSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            linearSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            linearSampler.ShaderRegister = 0;
            linearSampler.RegisterSpace = 0;
            linearSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC fsrSigDesc = {};
            fsrSigDesc.Version = m_highestRootSignatureVersion;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                fsrSigDesc.Desc_1_1.NumParameters = 2;
                fsrSigDesc.Desc_1_1.pParameters = rootParams;
                fsrSigDesc.Desc_1_1.NumStaticSamplers = 1;
                fsrSigDesc.Desc_1_1.pStaticSamplers = &linearSampler;
                fsrSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            } else {
                static D3D12_ROOT_PARAMETER rootParamsV10[2];
                static D3D12_DESCRIPTOR_RANGE srvRangeV10;
                srvRangeV10.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRangeV10.NumDescriptors = 1;
                srvRangeV10.BaseShaderRegister = 0;
                srvRangeV10.RegisterSpace = 0;

                rootParamsV10[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParamsV10[0].Descriptor.ShaderRegister = 0;
                rootParamsV10[0].Descriptor.RegisterSpace = 0;
                rootParamsV10[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                rootParamsV10[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rootParamsV10[1].DescriptorTable.NumDescriptorRanges = 1;
                rootParamsV10[1].DescriptorTable.pDescriptorRanges = &srvRangeV10;
                rootParamsV10[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                fsrSigDesc.Desc_1_0.NumParameters = 2;
                fsrSigDesc.Desc_1_0.pParameters = rootParamsV10;
                fsrSigDesc.Desc_1_0.NumStaticSamplers = 1;
                fsrSigDesc.Desc_1_0.pStaticSamplers = &linearSampler;
                fsrSigDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            }

            CreateRootSignature(&fsrSigDesc, &m_fsrRootSignature);

            ComPtr<IDxcBlob> fsrVS = CompileShaderDXC(L"Contents/shaders/fsr_upscale.hlsl", L"VSMain", L"vs_6_5");
            ComPtr<IDxcBlob> fsrPS = CompileShaderDXC(L"Contents/shaders/fsr_upscale.hlsl", L"PSMain", L"ps_6_5");
            if (fsrVS && fsrPS) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.InputLayout = { nullptr, 0 };
                psoDesc.pRootSignature = m_fsrRootSignature.Get();
                psoDesc.VS = { fsrVS->GetBufferPointer(), fsrVS->GetBufferSize() };
                psoDesc.PS = { fsrPS->GetBufferPointer(), fsrPS->GetBufferSize() };
                psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                psoDesc.DepthStencilState.DepthEnable = FALSE;
                psoDesc.DepthStencilState.StencilEnable = FALSE;
                psoDesc.SampleMask = UINT_MAX;
                psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                psoDesc.NumRenderTargets = 1;
                psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                psoDesc.SampleDesc.Count = 1;
                m_pipelineCache.Graphics("m_fsrPipelineState",&psoDesc,IID_PPV_ARGS(&m_fsrPipelineState));
            }

            // Create FSR Constant Buffer
            UINT cbSize = ALIGN_UP(sizeof(FSRConstants), 256);
            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);
            if (SUCCEEDED(m_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_fsrConstantBuffer)))) {
                CD3DX12_RANGE readRange(0, 0);
                m_fsrConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pFsrCbvDataBegin));
            }
        }

        // --- Volumetric Fog Compute Pipeline ---
        {
            CD3DX12_DESCRIPTOR_RANGE1 srvRange;
            srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0: Astra GI Radiance Atlas (for injection)
            srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

            CD3DX12_DESCRIPTOR_RANGE1 uavRange;
            uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0); // u0: Fog Volume, u1: Accumulation

            CD3DX12_ROOT_PARAMETER1 rootParams[3];
            rootParams[0].InitAsConstantBufferView(0); // b0: Scene Constants
            rootParams[1].InitAsDescriptorTable(1, &srvRange); // t0: Astra GI Radiance Atlas
            rootParams[2].InitAsDescriptorTable(1, &uavRange); // u0, u1: Fog Volume, Accumulation

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                sigDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            } else {
                static CD3DX12_ROOT_PARAMETER rootParamsV10[3];
                rootParamsV10[0].InitAsConstantBufferView(0);
                static CD3DX12_DESCRIPTOR_RANGE srvRangeV10;
                srvRangeV10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
                rootParamsV10[1].InitAsDescriptorTable(1, &srvRangeV10);
                static CD3DX12_DESCRIPTOR_RANGE uavRangeV10;
                uavRangeV10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
                rootParamsV10[2].InitAsDescriptorTable(1, &uavRangeV10);
                sigDesc.Init_1_0(3, rootParamsV10, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            }

            if (FAILED(CreateRootSignature(&sigDesc, &m_volumetricFogRootSignature))) {
                LOG("Volumetric Fog Root Signature creation failed");
            }

            ComPtr<IDxcBlob> injectCS = CompileShaderDXC(L"shaders/volumetric_fog.hlsl", L"InjectLightCS", L"cs_6_5");
            if (injectCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_volumetricFogRootSignature.Get();
                psoDesc.CS = { injectCS->GetBufferPointer(), injectCS->GetBufferSize() };
                m_pipelineCache.Compute("m_volumetricFogInjectPipelineState",&psoDesc,IID_PPV_ARGS(&m_volumetricFogInjectPipelineState));
            }

            ComPtr<IDxcBlob> accumulateCS = CompileShaderDXC(L"shaders/volumetric_fog.hlsl", L"AccumulateFogCS", L"cs_6_5");
            if (accumulateCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_volumetricFogRootSignature.Get();
                psoDesc.CS = { accumulateCS->GetBufferPointer(), accumulateCS->GetBufferSize() };
                m_pipelineCache.Compute("m_volumetricFogAccumulatePipelineState",&psoDesc,IID_PPV_ARGS(&m_volumetricFogAccumulatePipelineState));
            }
        }

        // --- SSR Compute Pipeline ---
        {
            CD3DX12_DESCRIPTOR_RANGE1 srvRange;
            srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // t0: HiZ Buffer

            CD3DX12_DESCRIPTOR_RANGE1 uavRange;
            uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE); // u0: Reflection Result

            CD3DX12_ROOT_PARAMETER1 rootParams[3];
            rootParams[0].InitAsConstantBufferView(0); // b0: Scene Constants
            rootParams[1].InitAsDescriptorTable(1, &srvRange);
            rootParams[2].InitAsDescriptorTable(1, &uavRange);

            D3D12_STATIC_SAMPLER_DESC ssrSampler = {};
            ssrSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            ssrSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ssrSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ssrSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ssrSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
            ssrSampler.MaxLOD = D3D12_FLOAT32_MAX;
            ssrSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc;
            if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                sigDesc.Init_1_1(_countof(rootParams), rootParams, 1, &ssrSampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            } else {
                static CD3DX12_ROOT_PARAMETER rootParamsV10[3];
                rootParamsV10[0].InitAsConstantBufferView(0);
                static CD3DX12_DESCRIPTOR_RANGE srvRangeV10, uavRangeV10;
                srvRangeV10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
                uavRangeV10.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
                rootParamsV10[1].InitAsDescriptorTable(1, &srvRangeV10);
                rootParamsV10[2].InitAsDescriptorTable(1, &uavRangeV10);
                static D3D12_STATIC_SAMPLER_DESC ssrSamplerV10;
                memcpy(&ssrSamplerV10, &ssrSampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
                sigDesc.Init_1_0(3, rootParamsV10, 1, &ssrSamplerV10, D3D12_ROOT_SIGNATURE_FLAG_NONE);
            }

            if (FAILED(CreateRootSignature(&sigDesc, &m_ssrRootSignature))) {
                LOG("SSR Root Signature creation failed");
            }

            ComPtr<IDxcBlob> ssrCS = CompileShaderDXC(L"shaders/ssr.hlsl", L"CSMain", L"cs_6_5");
            if (ssrCS) {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_ssrRootSignature.Get();
                psoDesc.CS = { ssrCS->GetBufferPointer(), ssrCS->GetBufferSize() };
                m_pipelineCache.Compute("m_ssrPipelineState",&psoDesc,IID_PPV_ARGS(&m_ssrPipelineState));
            }
        }

        {
            ComPtr<IDxcBlob> rvtVS = CompileShaderDXC(L"Contents/shaders/rvt.hlsl", L"VSMain", L"vs_6_5");
            ComPtr<IDxcBlob> rvtPS = CompileShaderDXC(L"Contents/shaders/rvt.hlsl", L"PSMain", L"ps_6_5");

            if (rvtVS && rvtPS && m_rvtBakeRootSignature) {
                static D3D12_INPUT_ELEMENT_DESC rvtInputLayout[] = {
                    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                    { "NORMAL", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
                };
                D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                psoDesc.pRootSignature = m_rvtBakeRootSignature.Get();
                psoDesc.InputLayout = { rvtInputLayout, _countof(rvtInputLayout) };
                psoDesc.VS = { rvtVS->GetBufferPointer(), rvtVS->GetBufferSize() };
                psoDesc.PS = { rvtPS->GetBufferPointer(), rvtPS->GetBufferSize() };
                psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                psoDesc.DepthStencilState.DepthEnable = FALSE;
                psoDesc.SampleMask = UINT_MAX;
                psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                psoDesc.NumRenderTargets = 2;
                psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
                psoDesc.SampleDesc.Count = 1;

                if (SUCCEEDED(m_pipelineCache.Graphics("m_rvtBakePipelineState",&psoDesc,IID_PPV_ARGS(&m_rvtBakePipelineState)))) {
                    LOG("RVT Bake PSO created successfully");
                }
            } else {
                LOG("RVT Bake: Skipping PSO - shaders not compiled or RootSig is null");
            }

            // --- RVT Decal Pipeline ---
            {
                LOG("RVT Decal: Start");

                D3D12_DESCRIPTOR_RANGE1 srvRange = {};
                srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRange.NumDescriptors = 100;
                srvRange.BaseShaderRegister = 0;
                srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

                D3D12_ROOT_PARAMETER1 params[2];
                params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                params[0].Descriptor = { 0, 0 };
                params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                
                params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                params[1].DescriptorTable.NumDescriptorRanges = 1;
                params[1].DescriptorTable.pDescriptorRanges = &srvRange;
                params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                D3D12_STATIC_SAMPLER_DESC sampler = {};
                sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
                rsDesc.Version = m_highestRootSignatureVersion;
                if (m_highestRootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                    rsDesc.Desc_1_1.NumParameters = 2;
                    rsDesc.Desc_1_1.pParameters = params;
                    rsDesc.Desc_1_1.NumStaticSamplers = 1;
                    rsDesc.Desc_1_1.pStaticSamplers = &sampler;
                    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
                } else {
                    static D3D12_ROOT_PARAMETER paramsV10[2];
                    static D3D12_DESCRIPTOR_RANGE rangesV10[1][1];
                    
                    paramsV10[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    paramsV10[0].Descriptor.ShaderRegister = 0;
                    paramsV10[0].Descriptor.RegisterSpace = 0;
                    paramsV10[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                    paramsV10[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    paramsV10[1].DescriptorTable.NumDescriptorRanges = 1;
                    rangesV10[0][0].RangeType = srvRange.RangeType;
                    rangesV10[0][0].NumDescriptors = srvRange.NumDescriptors;
                    rangesV10[0][0].BaseShaderRegister = srvRange.BaseShaderRegister;
                    rangesV10[0][0].RegisterSpace = srvRange.RegisterSpace;
                    rangesV10[0][0].OffsetInDescriptorsFromTableStart = srvRange.OffsetInDescriptorsFromTableStart;
                    paramsV10[1].DescriptorTable.pDescriptorRanges = rangesV10[0];
                    paramsV10[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

                    rsDesc.Desc_1_0.NumParameters = 2;
                    rsDesc.Desc_1_0.pParameters = paramsV10;
                    rsDesc.Desc_1_0.NumStaticSamplers = 1;
                    static D3D12_STATIC_SAMPLER_DESC samplerV10;
                    memcpy(&samplerV10, &sampler, sizeof(D3D12_STATIC_SAMPLER_DESC));
                    rsDesc.Desc_1_0.pStaticSamplers = &samplerV10;
                    rsDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
                }

                LOG("RVT Decal: Creating Root Signature");
                if (FAILED(CreateRootSignature(&rsDesc, &m_rvtDecalRootSignature))) {
                    LOG("RVT Decal Root Signature creation failed");
                } else {
                    LOG("RVT Decal Root Signature created");
                }

                LOG("RVT Decal: Compiling VS");
                ComPtr<IDxcBlob> dVS = CompileShaderDXC(L"Contents/shaders/rvt_decal.hlsl", L"VSMain", L"vs_6_5");
                if (!dVS) {
                    LOG("RVT Decal VS compilation failed");
                } else {
                    LOG("RVT Decal VS compiled");
                }

                LOG("RVT Decal: Compiling PS");
                ComPtr<IDxcBlob> dPS = CompileShaderDXC(L"Contents/shaders/rvt_decal.hlsl", L"PSMain", L"ps_6_5");
                if (!dPS) {
                    LOG("RVT Decal PS compilation failed");
                } else {
                    LOG("RVT Decal PS compiled");
                }

                if (dVS && dPS) {
                    static D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
                        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
                    };

                    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
                    psoDesc.pRootSignature = m_rvtDecalRootSignature.Get();
                    psoDesc.VS = { dVS->GetBufferPointer(), dVS->GetBufferSize() };
                    psoDesc.PS = { dPS->GetBufferPointer(), dPS->GetBufferSize() };
                    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
                    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

                    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
                    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
                    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
                    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                    psoDesc.BlendState.RenderTarget[1] = psoDesc.BlendState.RenderTarget[0];

                    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
                    psoDesc.DepthStencilState.DepthEnable = FALSE;
                    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

                    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
                    psoDesc.SampleMask = UINT_MAX;
                    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                    psoDesc.NumRenderTargets = 2;
                    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                    psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
                    psoDesc.SampleDesc.Count = 1;

                    LOG("RVT Decal: Creating PSO");
                    HRESULT hr = m_pipelineCache.Graphics("m_rvtDecalPipelineState",&psoDesc,IID_PPV_ARGS(&m_rvtDecalPipelineState));
                    if (FAILED(hr)) {
                        LOG("RVT Decal PSO creation failed: 0x" << std::hex << hr);
                    } else {
                        LOG("RVT Decal PSO created");
                    }
                } else {
                    LOG("RVT Decal: Skipping PSO due to shader compilation failure");
                }
            }
           
        }

        D3D12_RESOURCE_DESC ssaoCbDesc = {};
        ssaoCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ssaoCbDesc.Width = (sizeof(SSAOConstants) + 255) & ~255;
        ssaoCbDesc.Height = 1;
        ssaoCbDesc.DepthOrArraySize = 1;
        ssaoCbDesc.MipLevels = 1;
        ssaoCbDesc.Format = DXGI_FORMAT_UNKNOWN;
        ssaoCbDesc.SampleDesc.Count = 1;
        ssaoCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(m_device->CreateCommittedResource(
            &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ssaoCbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_ssaoConstantBuffer)))) { return false; }
        if (m_ssaoConstantBuffer) m_ssaoConstantBuffer->SetName(L"SSAOConstantBuffer");

        D3D12_RANGE ssaoReadRange = {0, 0};
        if (FAILED(m_ssaoConstantBuffer->Map(0, &ssaoReadRange, reinterpret_cast<void**>(&m_pSSAOCbvDataBegin)))) { return false; }

        D3D12_RESOURCE_DESC blurCbDesc = ssaoCbDesc;
        blurCbDesc.Width = (sizeof(SSAOBlurConstants) + 255) & ~255;
        if (FAILED(m_device->CreateCommittedResource(
            &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &blurCbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_ssaoBlurConstantBuffer)))) { return false; }
        if (m_ssaoBlurConstantBuffer) m_ssaoBlurConstantBuffer->SetName(L"SSAOBlurConstantBuffer");
        if (FAILED(m_ssaoBlurConstantBuffer->Map(0, &ssaoReadRange, reinterpret_cast<void**>(&m_pSSAOBlurCbvDataBegin)))) { return false; }

        D3D12_RESOURCE_DESC ssgiCbDesc = {};
        ssgiCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ssgiCbDesc.Width = (sizeof(SSGIConstantBuffer) + 255) & ~255;
        ssgiCbDesc.Height = 1;
        ssgiCbDesc.DepthOrArraySize = 1;
        ssgiCbDesc.MipLevels = 1;
        ssgiCbDesc.Format = DXGI_FORMAT_UNKNOWN;
        ssgiCbDesc.SampleDesc.Count = 1;
        ssgiCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ssgiCbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (FAILED(m_device->CreateCommittedResource(
            &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ssgiCbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_ssgiConstantBuffer)))) { return false; }
        if (m_ssgiConstantBuffer) m_ssgiConstantBuffer->SetName(L"SSGIConstantBuffer");
        if (FAILED(m_ssgiConstantBuffer->Map(0, &ssaoReadRange, reinterpret_cast<void**>(&m_pSsgiCbvDataBegin)))) { return false; }

        LOG("SSAO: Creating Heaps");
        D3D12_DESCRIPTOR_HEAP_DESC ssaoSrvHeapDesc = {};
        ssaoSrvHeapDesc.NumDescriptors = 1;
        ssaoSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ssaoSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&ssaoSrvHeapDesc, IID_PPV_ARGS(&m_ssaoSrvHeap)))) { return false; }
        LOG("SSAO: Heaps Created");

        D3D12_DESCRIPTOR_HEAP_DESC ssgiSrvHeapDesc = {};
        ssgiSrvHeapDesc.NumDescriptors = 5; // Normal, Depth, Albedo, Output(UAV)
        ssgiSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ssgiSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&ssgiSrvHeapDesc, IID_PPV_ARGS(&m_ssgiSrvHeap)))) { return false; }

        D3D12_DESCRIPTOR_HEAP_DESC blurSrvHeapDesc = {};
        blurSrvHeapDesc.NumDescriptors = 2;
        blurSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        blurSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&blurSrvHeapDesc, IID_PPV_ARGS(&m_ssaoBlurSrvHeap)))) { return false; }
        LOG("SSAO: Creating render targets");

        LOG("SSAO RT dims: " << m_width << "x" << m_height << " RTV indices=" << m_ssaoRtvIndex << "," << m_ssaoBlurRtvIndex);
        auto CreateSSAOTarget = [&](ComPtr<ID3D12Resource>& resource, UINT rtvIndex) {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = m_width;
            desc.Height = m_height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            D3D12_CLEAR_VALUE clearVal = {};
            clearVal.Format = DXGI_FORMAT_R8_UNORM;
            clearVal.Color[0] = 1.0f; clearVal.Color[1] = 1.0f; clearVal.Color[2] = 1.0f; clearVal.Color[3] = 1.0f;

            HRESULT hr = m_device->CreateCommittedResource(
                &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
                IID_PPV_ARGS(&resource));

            if (FAILED(hr) || !resource) {
                LOG("SSAO: CreateCommittedResource FAILED. HRESULT: 0x" << std::hex << hr);
                return;
            }

            if (rtvIndex == m_ssaoRtvIndex) resource->SetName(L"SSAOTexture");
            else if (rtvIndex == m_ssaoBlurRtvIndex) resource->SetName(L"SSAOBlurTexture");

            D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += rtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(resource.Get(), nullptr, handle);
        };

        CreateSSAOTarget(m_ssaoTexture, m_ssaoRtvIndex);
        LOG("SSAO: ssaoTexture created");
        CreateSSAOTarget(m_ssaoBlurTexture, m_ssaoBlurRtvIndex);
        LOG("SSAO: ssaoBlurTexture created");

        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MipLevels = 1;

        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, m_ssaoSrvHeap->GetCPUDescriptorHandleForHeapStart());

        UINT srvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE blurSrvHandle = m_ssaoBlurSrvHeap->GetCPUDescriptorHandleForHeapStart();
        
        D3D12_SHADER_RESOURCE_VIEW_DESC ssaoTexSrvDesc = {};
        ssaoTexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ssaoTexSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
        ssaoTexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ssaoTexSrvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_ssaoTexture.Get(), &ssaoTexSrvDesc, blurSrvHandle);

        blurSrvHandle.ptr += srvDescSize;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, blurSrvHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapDepthHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        mainHeapDepthHandle.ptr += m_depthSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, mainHeapDepthHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapSSAOHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        mainHeapSSAOHandle.ptr += m_ssaoSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_ssaoTexture.Get(), &ssaoTexSrvDesc, mainHeapSSAOHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapSSAOBlurHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        mainHeapSSAOBlurHandle.ptr += m_ssaoBlurSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_ssaoBlurTexture.Get(), &ssaoTexSrvDesc, mainHeapSSAOBlurHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE ssgiHeapHandle = m_ssgiSrvHeap->GetCPUDescriptorHandleForHeapStart();
        // 0: g_NormalTex
        D3D12_SHADER_RESOURCE_VIEW_DESC normSrvDesc = {};
        normSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        normSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        normSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        normSrvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_normalTexture.Get(), &normSrvDesc, ssgiHeapHandle);
        
        // 1: g_DepthTex
        ssgiHeapHandle.ptr += srvDescSize;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, ssgiHeapHandle);
        
        // 2: g_AlbedoTex
        ssgiHeapHandle.ptr += srvDescSize;
        m_device->CreateShaderResourceView(m_sceneColorTexture.Get(), &normSrvDesc, ssgiHeapHandle);
        
        ssgiHeapHandle.ptr += srvDescSize;
        m_device->CreateShaderResourceView(m_rrDiffuseAlbedo.Get(), &normSrvDesc, ssgiHeapHandle);
        // 4: g_OutputTex (UAV)
        ssgiHeapHandle.ptr += srvDescSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        m_device->CreateUnorderedAccessView(m_ssgiTexture.Get(), nullptr, &uavDesc, ssgiHeapHandle);
    }

    LOG("SSAO init complete");

    LOG("Initializing Cloud Constants");
    {
        D3D12_RESOURCE_DESC cloudCbDesc = {};
        cloudCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cloudCbDesc.Width = (sizeof(CloudConstants) + 255) & ~255;
        cloudCbDesc.Height = 1;
        cloudCbDesc.DepthOrArraySize = 1;
        cloudCbDesc.MipLevels = 1;
        cloudCbDesc.Format = DXGI_FORMAT_UNKNOWN;
        cloudCbDesc.SampleDesc.Count = 1;
        cloudCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        cloudCbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (FAILED(m_device->CreateCommittedResource(
            &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &cloudCbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_cloudConstantBuffer)))) {
                        return false;
        }
        
        
        m_cloudConstantBuffer->SetName(L"Cloud Constant Buffer");
        m_cloudConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pCloudCbvDataBegin));
    }

    // --- GPU Profiling Initialization ---
    {
        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = MaxQueryCount;
        queryHeapDesc.NodeMask = 0;
        HRESULT hrQuery = m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap));
        if (FAILED(hrQuery)) {
            LOG("ERROR: Failed to create query heap: 0x" << std::hex << hrQuery);
        }

        D3D12_RESOURCE_DESC queryBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(MaxQueryCount * sizeof(UINT64));
        HRESULT hrQueryBuffer = m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &queryBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_queryResultBuffer));
        if (FAILED(hrQueryBuffer)) {
            LOG("ERROR: Failed to create query result buffer: 0x" << std::hex << hrQueryBuffer);
        }

        // Create readback buffer for query results
        D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(MaxQueryCount * sizeof(UINT64));
        D3D12_HEAP_PROPERTIES readbackHeap{};readbackHeap.Type=D3D12_HEAP_TYPE_READBACK;
        HRESULT hrReadback = m_device->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_queryReadbackBuffer));
        if (FAILED(hrReadback)) {
            LOG("ERROR: Failed to create query readback buffer: 0x" << std::hex << hrReadback);
        }
        
        m_commandQueue->GetTimestampFrequency(&m_timestampFrequency);
    }

    // --- VRS Support Check ---
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 vrsOptions = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &vrsOptions, sizeof(vrsOptions)))) {
        m_vrsSupported = (vrsOptions.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_1);
        LOG("VRS Supported: " << (m_vrsSupported ? "YES" : "NO") << " (Tier " << vrsOptions.VariableShadingRateTier << ")");
    }

    // --- Conservative Raster Check ---
    D3D12_FEATURE_DATA_D3D12_OPTIONS vrsOptionsLegacy = {}; // Conservative raster is in the base options or OPTIONS
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &vrsOptionsLegacy, sizeof(vrsOptionsLegacy)))) {
        m_conservativeRasterSupported = (vrsOptionsLegacy.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_1);
        LOG("Conservative Raster Supported: " << (m_conservativeRasterSupported ? "YES" : "NO"));
    }

#ifndef ENGINE_RELEASE
    LOG("Initializing ImGui");
    {
        D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
        imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        imguiHeapDesc.NumDescriptors = 1024;
        imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&imguiHeapDesc, IID_PPV_ARGS(&m_imguiSrvHeap)))) return false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; // Managed via encrypted & compressed Save1.save
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(hwnd);
        
        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = m_device.Get();
        initInfo.CommandQueue = m_commandQueue.Get();
        if (m_commandQueue) m_commandQueue->SetName(L"MainCommandQueue");
        initInfo.NumFramesInFlight = FrameCount;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        initInfo.SrvDescriptorHeap = m_imguiSrvHeap.Get();
        initInfo.LegacySingleSrvCpuDescriptor = m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();
        initInfo.LegacySingleSrvGpuDescriptor = m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();
        
        ImGui_ImplDX12_Init(&initInfo);
        m_imguiInitialized = true;
    }
#endif

    m_commandList->Close();
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    LOG("Executing Command Lists and Waiting");
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    
    WaitForPreviousFrame();
    LOG("Wait for frame done");

    LOG("Calling Resize");
    Resize(m_width, m_height);
    LOG("Resize done");


    
    LOG("Checking DXR Device");
    if (m_dxrDevice) {
        // Re-open the command list (was closed+executed before Resize)
        m_commandAllocator->Reset();
        m_commandList->Reset(m_commandAllocator.Get(), nullptr);

        CreateBLAS();
        CreateTLAS();

        // Close and execute the DXR build commands
        m_commandList->Close();
        {
            ID3D12CommandList* ppDxrCmds[] = { m_commandList.Get() };
            m_commandQueue->ExecuteCommandLists(1, ppDxrCmds);
        }
        WaitForPreviousFrame();

        CreateRayTracingOutputResource();
        CreateRayTracingPipeline();
        CreateShaderTable();
    }

    EngineRain::RainSystem::Get().Initialize(m_device.Get());
    if (!EngineVFX::ParticleSystem::Get().Initialize(m_device.Get()))
        std::cerr << "[VFX] Particle rendering initialization failed; effects will not be visible.\n";
    InitXeSS();InitDirectStorage();InitFSR2();if(m_upscaleTechnique!=UpscaleTechnique::FSR)ShutdownFSR2();

    m_pipelineCache.Flush();
    AdvancedShaderDelivery::ReportStatistics(m_device.Get());
    LOG("Renderer::Initialize complete");
    return true;
}

void Renderer::ProcessLoadedTextures() {
    if (!m_textureStreamer) return;

    m_textureStreamer->Update();

    auto newTextures = m_textureStreamer->GetNewlyUploadedTextures();
    if (newTextures.empty()) return;

    // GPU-side wait: ensure copy queue finishes uploading textures before 3D engine reads them
    uint64_t uploadFence=0;for(const auto& texture:newTextures)uploadFence=(std::max)(uploadFence,texture->GetFenceValue());if(uploadFence)m_commandQueue->Wait(m_textureStreamer->GetFence(),uploadFence);

    D3D12_CPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    
    for (auto& tex : newTextures) {
        if(m_nextSrvIndex>=1000){tex->SetState(TextureState::Error);LOG("Material texture descriptor capacity reached");continue;}
        UINT srvIndex = m_nextSrvIndex++;
        tex->SetSrvIndex(srvIndex);

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mainHeapHandle;
        srvHandle.ptr += srvIndex * m_cbvSrvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = tex->GetResource()->GetDesc().MipLevels;
        m_device->CreateShaderResourceView(tex->GetResource(), &srvDesc, srvHandle);
        tex->SetState(TextureState::Ready);
    }
}

std::shared_ptr<Texture> Renderer::LoadTexture(const std::string& filepath) {
    if (!m_textureStreamer) return nullptr;
    auto key=TextureCacheKey(filepath);
    if(auto it=m_textureCache.find(key);it!=m_textureCache.end())if(auto cached=it->second.lock())return cached;
    auto tex = m_textureStreamer->LoadTexture(filepath);
    if (tex) {m_textureCache[key]=tex;m_textures.push_back(tex);}
    return tex;
}

void Renderer::SetAnisotropy(int aniso) {
    m_anisotropy = (std::clamp)(aniso, 1, 16);
    if (m_samplerHeap && m_device) {
        D3D12_SAMPLER_DESC rtSamplerDesc = {};
        if (m_anisotropy <= 1) {
            rtSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            rtSamplerDesc.MaxAnisotropy = 1;
        } else {
            rtSamplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
            rtSamplerDesc.MaxAnisotropy = (UINT)m_anisotropy;
        }
        rtSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        rtSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        rtSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        rtSamplerDesc.MipLODBias = 0.0f;
        rtSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        rtSamplerDesc.MinLOD = 0.0f;
        rtSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        m_device->CreateSampler(&rtSamplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
    }
}



void Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    m_width = width;
    m_height = height;
    m_renderWidth = width;
    m_renderHeight = height;
    m_framesAfterResize = 10;
    m_sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_rayTracingOutputState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_compositedColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_ssaoState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_ssaoBlurState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_depthStencilState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    m_mboitMomentsState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (m_upscaleTechnique == UpscaleTechnique::FSR) {
        float scale = 1.0f;
        switch (m_upscaleQuality) {
            case UpscaleQuality::Quality: scale = 1.5f; break;
            case UpscaleQuality::Balanced: scale = 1.7f; break;
            case UpscaleQuality::Performance: scale = 2.0f; break;
            case UpscaleQuality::UltraPerformance: scale = 3.0f; break;
            case UpscaleQuality::Native: scale = 1.0f; break;
            default: scale = 1.0f; break;
        }
        m_renderWidth = (std::max)(1, (int)(width / scale));
        m_renderHeight = (std::max)(1, (int)(height / scale));
    } else if (m_upscaleTechnique == UpscaleTechnique::DLSS) {
        sl::DLSSOptions dlssOpts = {};
        switch (m_upscaleQuality) {
            case UpscaleQuality::Quality: dlssOpts.mode = sl::DLSSMode::eMaxQuality; break;
            case UpscaleQuality::Balanced: dlssOpts.mode = sl::DLSSMode::eBalanced; break;
            case UpscaleQuality::Performance: dlssOpts.mode = sl::DLSSMode::eMaxPerformance; break;
            case UpscaleQuality::UltraPerformance: dlssOpts.mode = sl::DLSSMode::eUltraPerformance; break;
            case UpscaleQuality::Native: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
            default: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
        }
        dlssOpts.outputWidth = width;
        dlssOpts.outputHeight = height;
        
        sl::DLSSOptimalSettings dlssSettings = {};
        if (slDLSSGetOptimalSettings(dlssOpts, dlssSettings) == sl::Result::eOk) {
            m_renderWidth = (int)dlssSettings.optimalRenderWidth;
            m_renderHeight = (int)dlssSettings.optimalRenderHeight;
        }
        m_dlssNeedsReset = true;
    }

    if (!m_swapChain) return;
    if(m_fidelityFXFrameGeneration)m_fidelityFXFrameGeneration->Disable();
    if(m_dlssGConfigured){sl::DLSSGOptions off{};off.mode=sl::DLSSGMode::eOff;slDLSSGSetOptions(sl::ViewportHandle{0},off);m_dlssGConfigured=false;}
    if(m_dlssGFence&&m_dlssGFenceValue)m_commandQueue->Wait(m_dlssGFence.Get(),m_dlssGFenceValue);
    if(m_upscaleTechnique==UpscaleTechnique::XeSS&&m_xessSystem){m_xessSystem->Resize(width,height,EngineRender::XeSSSystem::EngineQualityToXeSS(int(m_upscaleQuality)));auto input=m_xessSystem->GetInputResolution();m_renderWidth=(std::max)(1u,input.x);m_renderHeight=(std::max)(1u,input.y);m_xessNeedsReset=true;}
    m_taaHistoryValid=false;m_firstFrameAfterResize=true;

    WaitForPreviousFrame();

    // Only resize swap chain if window size changed (or first time)
    static int lastWindowWidth = 0;
    static int lastWindowHeight = 0;
    
    if (width != lastWindowWidth || height != lastWindowHeight || !m_renderTargets[0]) {
        if (m_dlssSupported) {
            sl::ViewportHandle vHandle = { 0 };
            slFreeResources(sl::kFeatureDLSS, vHandle);
            slFreeResources(sl::kFeatureReflex, vHandle);
        }

        // 1. Synchronize and ensure GPU is completely idle before resizing buffers
        {
            const UINT64 fence = m_fenceValue;
            m_commandQueue->Signal(m_fence.Get(), fence);
            m_fenceValue++;
            if (m_fence->GetCompletedValue() < fence) {
                m_fence->SetEventOnCompletion(fence, m_fenceEvent);
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
        }

        // 2. Clear all references to backbuffers from Command Allocator and Command List
        if (m_commandAllocator) {
            m_commandAllocator->Reset();
        }
        if (m_commandList && m_commandAllocator) {
            m_commandList->Reset(m_commandAllocator.Get(), nullptr);
            m_commandList->Close();
        }

        // 3. Release backbuffer ComPtrs
        for (UINT n = 0; n < FrameCount; n++) {
            m_renderTargets[n].Reset();
        }

        LOG("Resize: Calling ResizeBuffers width=" << width << " height=" << height);
        UINT swapChainFlags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        LOG("Resize: flags=0x" << std::hex << swapChainFlags << std::dec);
        
        // Pass 0 for buffer count to preserve existing count in DX12 flip model
        HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags);
        if (FAILED(hr)) {
            LOG("ResizeBuffers with 0 count failed (0x" << std::hex << hr << "), trying with FrameCount=" << FrameCount << "...");
            hr = m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags);
        }
        if (FAILED(hr) && (swapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) {
            // Try fallback without tearing flag
            LOG("ResizeBuffers with tearing failed (0x" << std::hex << hr << "), trying without tearing...");
            hr = m_swapChain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        }

        if (FAILED(hr)) {
            LOG("ResizeBuffers FAILED with HRESULT: 0x" << std::hex << hr << std::dec << " Width: " << width << " Height: " << height);
            LogD3D12Messages();
        } else {
            LOG("ResizeBuffers SUCCEEDED Width: " << width << " Height: " << height);
            lastWindowWidth = width;
            lastWindowHeight = height;
        }

        if (m_swapChain) {
            m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            for (UINT n = 0; n < FrameCount; n++) {
                HRESULT gHr = m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]));
                if (SUCCEEDED(gHr)) {
                    m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
                    std::wstring name = L"BackBuffer_" + std::to_wstring(n);
                    m_renderTargets[n]->SetName(name.c_str());
                } else {
                    LOG("Resize: GetBuffer(" << n << ") failed: 0x" << std::hex << gHr);
                }
                rtvHandle.ptr += m_rtvDescriptorSize;
            }
            LOG("Resize: Back buffers RTVs recreated OK");
        }
    }

    LOG("Resize: Resetting depth stencil buffer");
    m_depthStencilBuffer.Reset();
    
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = m_renderWidth;
    depthStencilDesc.Height = m_renderHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = DXGI_FORMAT_D32_FLOAT;
    optClear.DepthStencil.Depth = 1.0f;

    LOG("Resize: Creating Depth Stencil Resource");
    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsDefault,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &optClear,
        IID_PPV_ARGS(&m_depthStencilBuffer)))) {
        LOG("Resize: CreateCommittedResource(Depth) FAILED");
        return;
    }
    m_depthStencilBuffer->SetName(L"DepthStencilBuffer");
    LOG("Resize: Depth resource created");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    LOG("Resize: DSV created");

    {
        D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        depthSrvHandle.ptr += m_depthSrvIndex * m_cbvSrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, depthSrvHandle);
        LOG("Resize: Depth SRV updated in main heap");
    }

    // Create / Rebind Shadow Map Depth Texture (2048x2048)
    if (!m_shadowMapTexture) {
        D3D12_RESOURCE_DESC shadowDesc = {};
        shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        shadowDesc.Width = 2048;
        shadowDesc.Height = 2048;
        shadowDesc.DepthOrArraySize = 1;
        shadowDesc.MipLevels = 1;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE shadowClear = {};
        shadowClear.Format = DXGI_FORMAT_D32_FLOAT;
        shadowClear.DepthStencil.Depth = 1.0f;

        if (SUCCEEDED(m_device->CreateCommittedResource(
            &m_heapPropsDefault,
            D3D12_HEAP_FLAG_NONE,
            &shadowDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &shadowClear,
            IID_PPV_ARGS(&m_shadowMapTexture)))) {
            m_shadowMapTexture->SetName(L"ShadowMapTexture");
            m_shadowMapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            LOG("Resize: Shadow Map Committed Resource created");
        } else {
            LOG("ERROR: directional shadow map allocation failed (2048x2048 D32)");
            LogD3D12Messages();
        }
    }

    if (m_shadowMapTexture) {
        // DSV at slot 1 in m_dsvHeap
        D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc = {};
        shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        shadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        shadowDsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        shadowDsvHandle.ptr += 1 * m_dsvDescriptorSize;
        m_device->CreateDepthStencilView(m_shadowMapTexture.Get(), &shadowDsvDesc, shadowDsvHandle);

        // SRV at m_vsmPhysicalAtlasSrvIndex (1501, t4 in space1)
        D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
        shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shadowSrvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        shadowSrvHandle.ptr += m_vsmPhysicalAtlasSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_shadowMapTexture.Get(), &shadowSrvDesc, shadowSrvHandle);
        LOG("Resize: Shadow Map DSV & SRV created successfully");
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    m_constantBufferData.screenRes = XMFLOAT2((float)m_renderWidth, (float)m_renderHeight);
    cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = 64 * 1024;
    D3D12_CPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateConstantBufferView(&cbvDesc, mainHeapHandle);
    LOG("Resize: CBV created");

    m_sceneColorTexture.Reset();
    m_velocityTexture.Reset();
    m_normalTexture.Reset();
    m_upscaledColorTexture.Reset();
    m_compositedColorTexture.Reset();
    m_ssaoTexture.Reset();
    m_ssaoBlurTexture.Reset();
    m_mboitMomentsTexture.Reset();
    m_rayTracingOutput.Reset();
    m_hizTexture.Reset();


    if (m_dxrDevice) {
        CreateRayTracingOutputResource();
    }

    auto CreateOffscreenTarget = [&](ComPtr<ID3D12Resource>& resource, DXGI_FORMAT format, UINT rtvIndex, UINT srvIndex, D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_STATES& stateTracker) {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_renderWidth;
        desc.Height = m_renderHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearVal = {};
        clearVal.Format = format;
        if (format == DXGI_FORMAT_R16G16_FLOAT || format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
             if (rtvIndex == m_velocityRtvIndex || rtvIndex == m_normalRtvIndex) {
                 clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 0.0f;
             } else {
                 clearVal.Color[0] = 0.2f; clearVal.Color[1] = 0.2f; clearVal.Color[2] = 0.2f; clearVal.Color[3] = 1.0f;
             }
        } else {
             clearVal.Color[0] = 0.0f; clearVal.Color[1] = 0.0f; clearVal.Color[2] = 0.0f; clearVal.Color[3] = 0.0f;
        }

        m_device->CreateCommittedResource(
            &m_heapPropsDefault,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            &clearVal,
            IID_PPV_ARGS(&resource));
            
        stateTracker = initialState;

        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += rtvIndex * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(resource.Get(), nullptr, handle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += srvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(resource.Get(), &srvDesc, srvHandle);
    };

    LOG("Resize: Creating offscreen render targets");
    LOG("Resize: SceneColor");
    CreateOffscreenTarget(m_sceneColorTexture, DXGI_FORMAT_R16G16B16A16_FLOAT, m_sceneColorRtvIndex, m_sceneColorSrvIndex, D3D12_RESOURCE_STATE_RENDER_TARGET, m_sceneColorState);
    if (m_sceneColorTexture) m_sceneColorTexture->SetName(L"SceneColorTexture");
    
    LOG("Resize: CompositedColor");
    CreateOffscreenTarget(m_compositedColorTexture, DXGI_FORMAT_R16G16B16A16_FLOAT, m_compositedColorRtvIndex, m_compositedColorSrvIndex, D3D12_RESOURCE_STATE_RENDER_TARGET, m_compositedColorState);
    if (m_compositedColorTexture) m_compositedColorTexture->SetName(L"CompositedColorTexture");
    
    LOG("Resize: Velocity");
    CreateOffscreenTarget(m_velocityTexture, DXGI_FORMAT_R16G16_FLOAT, m_velocityRtvIndex, m_velocitySrvIndex, D3D12_RESOURCE_STATE_RENDER_TARGET, m_velocityState);
    if (m_velocityTexture) m_velocityTexture->SetName(L"VelocityTexture");

    LOG("Resize: TAA History Buffer");
    // TAA History Buffer
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_renderWidth;
        desc.Height = m_renderHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&m_historyColorTexture));
        
        if (m_historyColorTexture) {
            m_historyColorTexture->SetName(L"TAAHistoryBuffer");

            // SRV
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvHandle.ptr += m_historyColorSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_historyColorTexture.Get(), &srvDesc, srvHandle);

            // UAV
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            
            D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            uavHandle.ptr += m_historyColorUavIndex * m_cbvSrvDescriptorSize;
            m_device->CreateUnorderedAccessView(m_upscaledColorTexture.Get(), nullptr, &uavDesc, uavHandle);
        } else {
            LOG("Resize: TAA history CreateCommittedResource FAILED");
        }
    }

    LOG("Resize: NormalTexture");
    CreateOffscreenTarget(m_normalTexture, DXGI_FORMAT_R16G16B16A16_FLOAT, m_normalRtvIndex, m_normalSrvIndex, D3D12_RESOURCE_STATE_RENDER_TARGET, m_normalState);
    if (m_normalTexture) m_normalTexture->SetName(L"NormalTexture");
    m_rrDiffuseAlbedo.Reset();m_rrSpecularAlbedo.Reset();CreateOffscreenTarget(m_rrDiffuseAlbedo,DXGI_FORMAT_R16G16B16A16_FLOAT,28,1510,D3D12_RESOURCE_STATE_RENDER_TARGET,m_rrDiffuseState);CreateOffscreenTarget(m_rrSpecularAlbedo,DXGI_FORMAT_R16G16B16A16_FLOAT,29,1511,D3D12_RESOURCE_STATE_RENDER_TARGET,m_rrSpecularState);

    LOG("Resize: SSGITexture");
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_renderWidth;
        desc.Height = m_renderHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&m_ssgiTexture));
        m_ssgiState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (m_ssgiTexture) m_ssgiTexture->SetName(L"SSGITexture");

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        uavHandle.ptr += m_ssgiUavIndex * m_cbvSrvDescriptorSize;
        
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        m_device->CreateUnorderedAccessView(m_ssgiTexture.Get(), nullptr, &uavDesc, uavHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += m_ssgiSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_ssgiTexture.Get(), &srvDesc, srvHandle);
    }

    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&m_upscaledColorTexture));
        
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += m_upscaledColorRtvIndex * m_rtvDescriptorSize;
        m_device->CreateRenderTargetView(m_upscaledColorTexture.Get(), nullptr, rtvHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        srvHandle.ptr += m_upscaledColorSrvIndex * m_cbvSrvDescriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_upscaledColorTexture.Get(), &srvDesc, srvHandle);
        if (m_upscaledColorTexture) m_upscaledColorTexture->SetName(L"UpscaledColorTexture");
    }

    LOG("Resize: Creating HiZ texture");
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_renderWidth;
        desc.Height = m_renderHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 0; // Full mip chain
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_hizTexture));
        // IMPORTANT: sync state tracker with the actual initial state used in CreateCommittedResource
        m_hizTextureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (m_hizTexture) {
            m_hizTexture->SetName(L"HiZTexture");

            UINT mipCount = m_hizTexture->GetDesc().MipLevels;
            for (UINT i = 0; i < mipCount; i++) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = i;

                D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
                uavHandle.ptr += (m_hizUavIndex + i) * m_cbvSrvDescriptorSize;
                m_device->CreateUnorderedAccessView(m_hizTexture.Get(), nullptr, &uavDesc, uavHandle);
                D3D12_SHADER_RESOURCE_VIEW_DESC mipSrv{};mipSrv.Format=DXGI_FORMAT_R32_FLOAT;
                mipSrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;mipSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                mipSrv.Texture2D.MostDetailedMip=i;mipSrv.Texture2D.MipLevels=1;
                auto mipHandle=m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();mipHandle.ptr+=UINT64(2100+i)*m_cbvSrvDescriptorSize;
                m_device->CreateShaderResourceView(m_hizTexture.Get(),&mipSrv,mipHandle);
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1; // Only mip 0 for SSR raymarching

            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvHandle.ptr += m_hizSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_hizTexture.Get(), &srvDesc, srvHandle);
        } else {
            LOG("Resize: HiZ CreateCommittedResource FAILED");
        }
    }

    // Volumetric Fog (3D Voxel Grid)
    LOG("Resize: Creating Volumetric Fog resources");
    {
        D3D12_RESOURCE_DESC fogDesc = {};
        fogDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        fogDesc.Alignment = 0;
        fogDesc.Width = 160;
        fogDesc.Height = 90;
        fogDesc.DepthOrArraySize = 128;
        fogDesc.MipLevels = 1;
        fogDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        fogDesc.SampleDesc.Count = 1;
        fogDesc.SampleDesc.Quality = 0;
        fogDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        fogDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &fogDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_volumetricFogVolume));
        if (m_volumetricFogVolume) m_volumetricFogVolume->SetName(L"VolumetricFogVolume");

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &fogDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_volumetricFogAccumulation));
        if (m_volumetricFogAccumulation) m_volumetricFogAccumulation->SetName(L"VolumetricFogAccumulation");

        // SRV for the volume (for sampling in pixel shader)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = fogDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        
        D3D12_CPU_DESCRIPTOR_HANDLE fogSrvHandle = mainHeapHandle;
        fogSrvHandle.ptr += m_volumetricFogSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_volumetricFogAccumulation.Get(), &srvDesc, fogSrvHandle); // Accumulation is what we sample

        // UAVs for compute shaders
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = fogDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize = 128;

        D3D12_CPU_DESCRIPTOR_HANDLE fogUavHandle = mainHeapHandle;
        fogUavHandle.ptr += m_volumetricFogUavIndex * m_cbvSrvDescriptorSize;
        m_device->CreateUnorderedAccessView(m_volumetricFogVolume.Get(), nullptr, &uavDesc, fogUavHandle);

        D3D12_CPU_DESCRIPTOR_HANDLE accUavHandle = mainHeapHandle;
        accUavHandle.ptr += m_volumetricFogAccumulationUavIndex * m_cbvSrvDescriptorSize;
        m_device->CreateUnorderedAccessView(m_volumetricFogAccumulation.Get(), nullptr, &uavDesc, accUavHandle);
    }

    // Hybrid Reflections (Composite Target)
    LOG("Resize: Creating Hybrid Reflections");
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
        desc.Width = m_renderWidth;
        desc.Height = m_renderHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_reflectionTexture));
        if (m_reflectionTexture) m_reflectionTexture->SetName(L"AstraReflectionTexture");

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();

        // SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mainHeapHandle;
        srvHandle.ptr += m_reflectionSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_reflectionTexture.Get(), &srvDesc, srvHandle);

        // UAV
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = mainHeapHandle;
        uavHandle.ptr += m_reflectionUavIndex * m_cbvSrvDescriptorSize;
        m_device->CreateUnorderedAccessView(m_reflectionTexture.Get(), nullptr, &uavDesc, uavHandle);
    }

    // Virtual Shadow Map (VSM) Physical Atlas
    LOG("Resize: Creating VSM Physical Atlas");
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 1; // Prototype atlas is not sampled by final shading
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hrVSM = m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_vsmPhysicalAtlas));
        if (FAILED(hrVSM)) {
            LOG("VSM Physical Atlas CreateCommittedResource FAILED: 0x" << std::hex << hrVSM);
        } else {
            m_vsmPhysicalAtlas->SetName(L"VSMPhysicalAtlas");
        }

        if (m_vsmPhysicalAtlas) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            
            D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            uavHandle.ptr += (m_vsmPhysicalAtlasUavIndex) * m_cbvSrvDescriptorSize;
            m_device->CreateUnorderedAccessView(m_vsmPhysicalAtlas.Get(), nullptr, &uavDesc, uavHandle);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvHandle.ptr += m_vsmPhysicalAtlasRtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(m_vsmPhysicalAtlas.Get(), nullptr, rtvHandle);
        }
    }

    // VSM Page Table
    LOG("Resize: Creating VSM Page Table");
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 512; // Example resolution for page table covering sun space
        desc.Height = 512;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hrVSMPage = m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&m_vsmPageTable));
        if (FAILED(hrVSMPage)) {
            LOG("VSM Page Table CreateCommittedResource FAILED: 0x" << std::hex << hrVSMPage);
        } else {
            m_vsmPageTable->SetName(L"VSMPageTable");
        }

        if (m_vsmPageTable) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvHandle.ptr += m_vsmPageTableSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_vsmPageTable.Get(), &srvDesc, srvHandle);

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

            D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            uavHandle.ptr += m_vsmPageTableUavIndex * m_cbvSrvDescriptorSize;
            m_device->CreateUnorderedAccessView(m_vsmPageTable.Get(), nullptr, &uavDesc, uavHandle);
        }
    }

    // --- Runtime Virtual Texturing (RVT) Resources ---
    LOG("Resize: Creating RVT Textures");
    {
        D3D12_RESOURCE_DESC atlasDesc = {};
        atlasDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        atlasDesc.Width = 1; // Prototype atlas is not sampled by final shading
        atlasDesc.Height = 1;
        atlasDesc.DepthOrArraySize = 1;
        atlasDesc.MipLevels = 1;
        atlasDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        atlasDesc.SampleDesc.Count = 1;
        atlasDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // Albedo Atlas
        HRESULT hrRVT1 = m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &atlasDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_rvtPhysicalAtlasAlbedo));
        if (FAILED(hrRVT1)) {
            LOG("RVT Albedo CreateCommittedResource FAILED: 0x" << std::hex << hrRVT1);
        } else {
            m_rvtPhysicalAtlasAlbedo->SetName(L"RVTPhysicalAtlasAlbedo");
        }
        
        if (m_rvtPhysicalAtlasAlbedo) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvAlbedo = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvAlbedo.ptr += m_rvtAlbedoAtlasRtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(m_rvtPhysicalAtlasAlbedo.Get(), nullptr, rtvAlbedo);
        }

        // Normal Atlas
        HRESULT hrRVT2 = m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &atlasDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_rvtPhysicalAtlasNormal));
        if (FAILED(hrRVT2)) {
            LOG("RVT Normal CreateCommittedResource FAILED: 0x" << std::hex << hrRVT2);
        } else {
            m_rvtPhysicalAtlasNormal->SetName(L"RVTPhysicalAtlasNormal");
        }

        if (m_rvtPhysicalAtlasNormal) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvNormal = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvNormal.ptr += m_rvtNormalAtlasRtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(m_rvtPhysicalAtlasNormal.Get(), nullptr, rtvNormal);
        }

        // Irradiance Atlas (Surface Cache)
        D3D12_RESOURCE_DESC irrAtlasDesc = atlasDesc;
        irrAtlasDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT; // HDR and compact
        HRESULT hrRVT3 = m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &irrAtlasDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_rvtPhysicalAtlasIrradiance));
        if (FAILED(hrRVT3)) {
            LOG("RVT Irradiance CreateCommittedResource FAILED: 0x" << std::hex << hrRVT3);
        } else {
            m_rvtPhysicalAtlasIrradiance->SetName(L"RVTPhysicalAtlasIrradiance");
        }

        if (m_rvtPhysicalAtlasIrradiance) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvIrr = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtvIrr.ptr += m_rvtIrradianceAtlasRtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(m_rvtPhysicalAtlasIrradiance.Get(), nullptr, rtvIrr);
        }

        // SRVs and UAVs for Atlases
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = atlasDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        if (m_rvtPhysicalAtlasAlbedo) {
            D3D12_CPU_DESCRIPTOR_HANDLE srvH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvH.ptr += m_rvtAlbedoAtlasSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_rvtPhysicalAtlasAlbedo.Get(), &srvDesc, srvH);
        }

        if (m_rvtPhysicalAtlasNormal) {
            D3D12_CPU_DESCRIPTOR_HANDLE srvH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvH.ptr += m_rvtNormalAtlasSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_rvtPhysicalAtlasNormal.Get(), &srvDesc, srvH);
        }

        if (m_rvtPhysicalAtlasIrradiance) {
            D3D12_CPU_DESCRIPTOR_HANDLE srvH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvH.ptr += m_rvtIrradianceAtlasSrvIndex * m_cbvSrvDescriptorSize;
            srvDesc.Format = irrAtlasDesc.Format;
            m_device->CreateShaderResourceView(m_rvtPhysicalAtlasIrradiance.Get(), &srvDesc, srvH);
        }

        // Page Table
        D3D12_RESOURCE_DESC ptDesc = atlasDesc;
        ptDesc.Width = 512; // 512x512 page table
        ptDesc.Height = 512;
        ptDesc.Format = DXGI_FORMAT_R32_UINT;
        ptDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hrRVTPage = m_device->CreateCommittedResource(&m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &ptDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_rvtPageTable));
        if (FAILED(hrRVTPage)) {
            LOG("RVT Page Table CreateCommittedResource FAILED: 0x" << std::hex << hrRVTPage);
        } else {
            m_rvtPageTable->SetName(L"RVTPageTable");
        }

        if (m_rvtPageTable) {
            D3D12_CPU_DESCRIPTOR_HANDLE srvH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvH.ptr += m_rvtPageTableSrvIndex * m_cbvSrvDescriptorSize;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = ptDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->CreateShaderResourceView(m_rvtPageTable.Get(), &srvDesc, srvH);

            D3D12_CPU_DESCRIPTOR_HANDLE uavH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            uavH.ptr += m_rvtPageTableUavIndex * m_cbvSrvDescriptorSize;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = ptDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_device->CreateUnorderedAccessView(m_rvtPageTable.Get(), nullptr, &uavDesc, uavH);
        }

        // UAV for Irradiance Atlas (needed by Astra Radiosity)
        if (m_rvtPhysicalAtlasIrradiance) {
            D3D12_CPU_DESCRIPTOR_HANDLE uavH = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            uavH.ptr += m_rvtIrradianceAtlasUavIndex * m_cbvSrvDescriptorSize;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT; 
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_device->CreateUnorderedAccessView(m_rvtPhysicalAtlasIrradiance.Get(), nullptr, &uavDesc, uavH);
        }
    }

    {
        D3D12_RESOURCE_DESC mboitDesc = {};
        mboitDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        mboitDesc.Width = m_renderWidth;
        mboitDesc.Height = m_renderHeight;
        mboitDesc.DepthOrArraySize = 1;
        mboitDesc.MipLevels = 1;
        mboitDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        mboitDesc.SampleDesc.Count = 1;
        mboitDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        mboitDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE mboitClear = {};
        mboitClear.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        mboitClear.Color[0] = 0; mboitClear.Color[1] = 0; mboitClear.Color[2] = 0; mboitClear.Color[3] = 0;

        HRESULT hrMBOIT = m_device->CreateCommittedResource(
            &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &mboitDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &mboitClear,
            IID_PPV_ARGS(&m_mboitMomentsTexture));
        if (FAILED(hrMBOIT)) {
            LOG("MBOIT CreateCommittedResource FAILED: 0x" << std::hex << hrMBOIT);
        } else {
            m_mboitMomentsTexture->SetName(L"MBOITMomentsTexture");
        }
        
        if (m_mboitMomentsTexture) {
            D3D12_CPU_DESCRIPTOR_HANDLE mboitHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            mboitHandle.ptr += m_mboitMomentsRtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(m_mboitMomentsTexture.Get(), nullptr, mboitHandle);

            D3D12_SHADER_RESOURCE_VIEW_DESC mboitSrvDesc = {};
            mboitSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            mboitSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            mboitSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            mboitSrvDesc.Texture2D.MipLevels = 1;
            
            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvHandle.ptr += m_mboitMomentsSrvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(m_mboitMomentsTexture.Get(), &mboitSrvDesc, srvHandle);
        }

        if (m_mboitMomentsTexture) m_mboitMomentsTexture->SetName(L"MBOITMomentsTexture");
    }

    {
        auto CreateSSAOTarget = [&](ComPtr<ID3D12Resource>& resource, UINT rtvIndex, D3D12_RESOURCE_STATES& stateTracker) {
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = m_renderWidth;
            desc.Height = m_renderHeight;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            D3D12_CLEAR_VALUE clearVal = {};
            clearVal.Format = DXGI_FORMAT_R8_UNORM;
            clearVal.Color[0] = 1.0f; clearVal.Color[1] = 1.0f; clearVal.Color[2] = 1.0f; clearVal.Color[3] = 1.0f;

            HRESULT hrCreate = m_device->CreateCommittedResource(
                &m_heapPropsDefault, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
                IID_PPV_ARGS(&resource));
                
            stateTracker = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

            if (FAILED(hrCreate) || !resource) {
                LOG("SSAO Resize: CreateCommittedResource FAILED. HRESULT: 0x" << std::hex << hrCreate);
                return;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += rtvIndex * m_rtvDescriptorSize;
            m_device->CreateRenderTargetView(resource.Get(), nullptr, handle);

            if (rtvIndex == m_ssaoRtvIndex) resource->SetName(L"SSAOTexture");
            else if (rtvIndex == m_ssaoBlurRtvIndex) resource->SetName(L"SSAOBlurTexture");
            
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = DXGI_FORMAT_R8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            UINT srvIndex = (rtvIndex == m_ssaoRtvIndex) ? m_ssaoSrvIndex : m_ssaoBlurSrvIndex;
            D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
            srvHandle.ptr += srvIndex * m_cbvSrvDescriptorSize;
            m_device->CreateShaderResourceView(resource.Get(), &srvDesc, srvHandle);
        };
        CreateSSAOTarget(m_ssaoTexture, m_ssaoRtvIndex, m_ssaoState);
        CreateSSAOTarget(m_ssaoBlurTexture, m_ssaoBlurRtvIndex, m_ssaoBlurState);

        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE mainHeapDepthHandle = m_mainDescHeap->GetCPUDescriptorHandleForHeapStart();
        mainHeapDepthHandle.ptr += m_depthSrvIndex * m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, mainHeapDepthHandle);

        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, m_ssaoSrvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_CPU_DESCRIPTOR_HANDLE blurSrvHandle = m_ssaoBlurSrvHeap->GetCPUDescriptorHandleForHeapStart();
        
        D3D12_SHADER_RESOURCE_VIEW_DESC ssaoTexSrvDesc = {};
        ssaoTexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ssaoTexSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
        ssaoTexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        ssaoTexSrvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_ssaoTexture.Get(), &ssaoTexSrvDesc, blurSrvHandle);

        blurSrvHandle.ptr += m_cbvSrvDescriptorSize;
        m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, blurSrvHandle);
    }

    if(m_ssgiSrvHeap&&m_ssgiTexture){
        // Resize replaces these resources; a private descriptor heap does not
        // inherit the refreshed descriptors in m_mainDescHeap.
        ID3D12Resource* sources[]={m_normalTexture.Get(),m_depthStencilBuffer.Get(),m_sceneColorTexture.Get(),m_rrDiffuseAlbedo.Get()};
        auto handle=m_ssgiSrvHeap->GetCPUDescriptorHandleForHeapStart();
        for(UINT i=0;i<4;++i){
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Format=i==1?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            m_device->CreateShaderResourceView(sources[i],&view,handle);handle.ptr+=m_cbvSrvDescriptorSize;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView(m_ssgiTexture.Get(),nullptr,&view,handle);
    }
    if(m_upscaleTechnique==UpscaleTechnique::FSR)InitFSR2();else ShutdownFSR2();
    LOG("Resize complete");
}

bool Renderer::IsDeviceRemoved() const {
    if (!m_device) return true;
    HRESULT hr = m_device->GetDeviceRemovedReason();
    if (hr != S_OK) {
        LOG("Device removed! Reason: 0x" << std::hex << hr);
        return true;
    }
    return false;
}

void Renderer::Render(float dt) {
    m_dlssRRActive = false; // Only successful evaluation marks this frame active.
    MarkLatencyStage(1);
    if (!m_device || m_deviceRemoved) return;
    m_lastDt = dt;
    m_cloudTime += dt;

    // Wait for previous frame to complete BEFORE starting new frame
    // This prevents DXGI_ERROR_INVALID_CALL when calling Present()
    if(!s_frameSynchronized)WaitForPreviousFrame();s_frameSynchronized=false;
    if(m_hasGpuFrameTiming&&m_queryReadbackBuffer&&m_timestampFrequency){
        void* data=nullptr;D3D12_RANGE range{0,12*sizeof(UINT64)};
        if(SUCCEEDED(m_queryReadbackBuffer->Map(0,&range,&data))){auto* times=static_cast<UINT64*>(data);if(times[1]>=times[0])m_lastGpuFrameMs=double(times[1]-times[0])*1000/double(m_timestampFrequency);const int begin[]={0,2,3,4,5,6,7,8,9,10};const int end[]={2,3,4,5,6,7,8,9,10,11};
            for(int i=0;i<10;++i)m_gpuPassTimes[i]=times[end[i]]>=times[begin[i]]?double(times[end[i]]-times[begin[i]])*1000/double(m_timestampFrequency):0;
            D3D12_RANGE written{0,0};m_queryReadbackBuffer->Unmap(0,&written);}
    }

    // Deferred resize: process pending quality/technique changes BEFORE opening command list
    if (m_pendingResize) {
        m_pendingResize = false;
        int width=m_pendingResizeWidth>0?m_pendingResizeWidth:m_width;int height=m_pendingResizeHeight>0?m_pendingResizeHeight:m_height;m_pendingResizeWidth=0;m_pendingResizeHeight=0;Resize(width,height);
    }

    UpdateTechnologySwapChain();
    auto* frameToken=m_slFrameToken;

    m_frameCounter++;
    if (m_device->GetDeviceRemovedReason() != S_OK) return;
    ProcessLoadedTextures();

    if (m_upscaleTechnique != UpscaleTechnique::None||m_taaEnabled) {
        m_camera->Jitter(m_frameCounter, m_renderWidth, m_renderHeight);
    } else {
        m_camera->SetJitter(0.0f, 0.0f);
    }

    XMMATRIX view = m_camera->GetViewMatrix();
    float fov = XMConvertToRadians(m_fov);
    float aspect = static_cast<float>(m_renderWidth) / m_renderHeight;
    XMMATRIX cleanProj = m_camera->GetCleanProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
    XMMATRIX proj = m_camera->GetProjectionMatrix(fov, aspect, 0.1f, 2000.0f);

    m_prevCleanViewProj = (m_frameCounter <= 2) ? (view * cleanProj) : m_cleanViewProj;
    m_cleanViewProj = view * cleanProj;
    m_viewProj = view * proj;

    auto& rainSys = EngineRain::RainSystem::Get();
    const auto& rainSettings = rainSys.GetSettings();
    if (rainSettings.enabled && rainSettings.surface.enabled) {
        m_constantBufferData.rainWetness = rainSettings.surface.wetnessEnabled 
            ? (rainSettings.surface.wetness * rainSettings.surface.wetnessStrength) : 0.0f;
        m_constantBufferData.rainPuddles = rainSettings.surface.puddlesEnabled 
            ? (rainSettings.surface.puddleAmount * rainSettings.surface.puddleAccumulation) : 0.0f;
    } else {
        m_constantBufferData.rainWetness = 0.0f;
        m_constantBufferData.rainPuddles = 0.0f;
    }
    m_constantBufferData.cloudDensity = m_cloudDensity;
    m_constantBufferData.cloudTime = m_cloudTime;
    float lightningFlash = rainSys.GetWeatherSystem().GetLightningFlash();
    if (lightningFlash > 0.01f) {
        m_constantBufferData.lightColor = XMFLOAT3(
            m_baseLightColor.x + lightningFlash * 3.5f,
            m_baseLightColor.y + lightningFlash * 4.0f,
            m_baseLightColor.z + lightningFlash * 4.5f
        );
    } else {
        m_constantBufferData.lightColor = m_baseLightColor;
    }
    m_constantBufferData.pad3 = (m_dxrEnabled && m_dxrDevice) ? 1.0f : 0.0f;
    m_constantBufferData.pad2 = (m_vsmEnabled&&m_shadowMapTexture&&m_vsmShadowPipelineState)?1.f:0.f;

    XMFLOAT3 camPos = m_camera->GetPosition();
    m_constantBufferData.cameraPos = camPos;
    m_constantBufferData.screenRes = XMFLOAT2((float)m_renderWidth, (float)m_renderHeight);
    m_constantBufferData.tanHalfFov = tanf(XMConvertToRadians(m_fov) * 0.5f);
    m_constantBufferData.viewProj = m_viewProj;
    m_constantBufferData.cleanViewProj = m_cleanViewProj;
    m_constantBufferData.prevCleanViewProj = m_prevCleanViewProj;

    static int logCounter = 0;
    if (++logCounter % 60 == 0) {
        LOG("Camera: pos=(" << camPos.x << ", " << camPos.y << ", " << camPos.z << ")");
    }

    // 0. Compute Sun Light Direction & View-Projection Matrix for Directional Shadows
    XMVECTOR sunDir = XMVector3Normalize(XMLoadFloat3(&m_constantBufferData.lightDir));
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    XMVECTOR shadowTarget = camPosVec;
    XMVECTOR shadowCamPos = shadowTarget - sunDir * 150.0f;
    XMVECTOR shadowUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (fabs(XMVectorGetY(sunDir)) > 0.99f) {
        shadowUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }
    XMMATRIX sunView = XMMatrixLookAtLH(shadowCamPos, shadowTarget, shadowUp);
    
    // Shadow Map Texel Snapping (locks shadow texels to integer world grid, preventing shimmering during walking)
    float shadowExtent = 120.0f;
    float shadowResolution = 2048.0f;
    float worldUnitsPerTexel = shadowExtent / shadowResolution;

    XMVECTOR shadowTargetLight = XMVector3Transform(shadowTarget, sunView);
    float targetX = XMVectorGetX(shadowTargetLight);
    float targetY = XMVectorGetY(shadowTargetLight);
    float snappedX = floorf(targetX / worldUnitsPerTexel) * worldUnitsPerTexel;
    float snappedY = floorf(targetY / worldUnitsPerTexel) * worldUnitsPerTexel;
    float diffX = snappedX - targetX;
    float diffY = snappedY - targetY;

    XMMATRIX sunProj = XMMatrixOrthographicLH(shadowExtent, shadowExtent, 1.0f, 400.0f);
    XMMATRIX snapMatrix = XMMatrixTranslation(diffX, diffY, 0.0f);
    XMMATRIX sunViewProj = sunView * snapMatrix * sunProj;
    m_constantBufferData.sunViewProj = sunViewProj;
    EngineRender::UploadSceneLights(m_constantBufferData,m_pointLights,m_spotLights);

    if (m_pCbvDataBegin) {
        memcpy(m_pCbvDataBegin, &m_constantBufferData, sizeof(SceneConstantBuffer));
    }

    if (!m_swapChain) { LOG("Render Error: SwapChain is null"); return; }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    LOG("Render: Got back buffer index " << m_frameIndex);

    if (!m_commandAllocator) { LOG("Render Error: CommandAllocator is null"); return; }
    LOG("Render: Resetting command allocator");
    m_commandAllocator->Reset();
    LOG("Render: Resetting command list");
    if (!m_commandList) { LOG("Render Error: CommandList is null"); return; }
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
    if(m_queryHeap&&m_queryReadbackBuffer)m_commandList->EndQuery(m_queryHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto gpuStamp=[&](UINT index){if(m_queryHeap&&m_queryReadbackBuffer)m_commandList->EndQuery(m_queryHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index);};
    if(!m_dxrAvailable){m_dxrEnabled=false;m_ssgiEnabled=false;}
    if(m_cullingProbeOutside)m_vsmEnabled=false;
    // Update DXR Raytracing acceleration structures at the beginning of the frame
    if (m_dxrEnabled && m_dxrDevice) {
        if (m_rtSceneDirty || !m_tlas) {
            RebuildRaytracingScene();
        } else {
            CreateTLAS();
        }
    }

    UINT cbStride = (sizeof(SceneConstantBuffer) + 255) & ~255;
    UINT currentCbIndex = 0;

    gpuStamp(2);
    // --- Mesh Shader Directional Shadow Map Pass ---
    if (m_vsmEnabled && m_shadowMapTexture && m_vsmShadowPipelineState) {
        Transition(m_commandList.Get(), m_shadowMapTexture.Get(), m_shadowMapState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        
        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        shadowDsv.ptr += 1 * m_dsvDescriptorSize;
        m_commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);

        D3D12_VIEWPORT shadowViewport = { 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
        D3D12_RECT shadowScissor = { 0, 0, 2048, 2048 };
        m_commandList->RSSetViewports(1, &shadowViewport);
        m_commandList->RSSetScissorRects(1, &shadowScissor);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        m_commandList->SetPipelineState(m_vsmShadowPipelineState.Get());

        ID3D12DescriptorHeap* shadowHeaps[] = { m_mainDescHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, shadowHeaps);

        if (m_vertexBuffer) m_commandList->SetGraphicsRootShaderResourceView(2, m_vertexBuffer->GetGPUVirtualAddress());
        if (m_indexBuffer) m_commandList->SetGraphicsRootShaderResourceView(3, m_indexBuffer->GetGPUVirtualAddress());
        if (m_meshletsBuffer) m_commandList->SetGraphicsRootShaderResourceView(4, m_meshletsBuffer->GetGPUVirtualAddress());

        // Render static models to shadow map
        for (auto& model : m_models) {
            if (!model.visible) continue;
            if (model.name == "Floor") continue; // Ground receives shadows, but does not cast self-shadows
            XMMATRIX scale = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
            XMMATRIX rot = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(model.rotation.x),
                XMConvertToRadians(model.rotation.y),
                XMConvertToRadians(model.rotation.z)
            );
            XMMATRIX trans = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
            XMMATRIX world = scale * rot * trans;
            XMStoreFloat4x4(&model.worldMatrix, world);

            SceneConstantBuffer cbData = m_constantBufferData;
            cbData.viewProj = sunViewProj;
            cbData.model = world;
            cbData.meshletStart = model.meshletStart;
            cbData.meshletCount = model.meshletCount;
            cbData.baseVertexLocation = model.baseVertexLocation;
            cbData.startIndexLocation=model.startIndexLocation;

            currentCbIndex++;
            UINT64 cbOffset = (UINT64)currentCbIndex * cbStride;
            if (m_pCbvDataBegin && cbOffset + sizeof(SceneConstantBuffer) <= 1024 * 1024) {
                memcpy(reinterpret_cast<UINT8*>(m_pCbvDataBegin) + cbOffset, &cbData, sizeof(SceneConstantBuffer));
            }
            m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + cbOffset);
            uint32_t asGroupCount = (model.meshletCount + 31) / 32;
            if(!DrawTessellated(model.material,model.indexCount,true,false))DrawGeometry(asGroupCount,model.indexCount);
        }

        // Render dynamic models to shadow map
        for (auto& model : m_dynamicModels) {
            if (!model.visible) continue;
            XMMATRIX scale = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
            XMMATRIX rot = XMMatrixRotationRollPitchYaw(
                XMConvertToRadians(model.rotation.x),
                XMConvertToRadians(model.rotation.y),
                XMConvertToRadians(model.rotation.z)
            );
            XMMATRIX trans = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
            XMMATRIX world = scale * rot * trans;

            SceneConstantBuffer cbData = m_constantBufferData;
            cbData.viewProj = sunViewProj;
            cbData.model = world;
            cbData.meshletStart = 0;
            cbData.meshletCount = model.meshletCount;
            cbData.baseVertexLocation = 0;
            cbData.startIndexLocation=0;

            currentCbIndex++;
            UINT64 cbOffset = (UINT64)currentCbIndex * cbStride;
            if (m_pCbvDataBegin && cbOffset + sizeof(SceneConstantBuffer) <= 1024 * 1024) {
                memcpy(reinterpret_cast<UINT8*>(m_pCbvDataBegin) + cbOffset, &cbData, sizeof(SceneConstantBuffer));
            }
            m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + cbOffset);
            if (model.vertexBuffer && model.indexBuffer && model.meshletBuffer) {
                m_commandList->SetGraphicsRootShaderResourceView(2, model.vertexBuffer->GetGPUVirtualAddress());
                m_commandList->SetGraphicsRootShaderResourceView(3, model.indexBuffer->GetGPUVirtualAddress());
                m_commandList->SetGraphicsRootShaderResourceView(4, model.meshletBuffer->GetGPUVirtualAddress());
            }
            uint32_t asGroupCount = (model.meshletCount + 31) / 32;
            if (asGroupCount > 0) {
                if(!DrawTessellated(model.material,model.indexCount,true,false))DrawGeometry(asGroupCount,model.indexCount);
            }
        }

        // Restore static buffer bindings for subsequent passes
        if (m_vertexBuffer) m_commandList->SetGraphicsRootShaderResourceView(2, m_vertexBuffer->GetGPUVirtualAddress());
        if (m_indexBuffer) m_commandList->SetGraphicsRootShaderResourceView(3, m_indexBuffer->GetGPUVirtualAddress());
        if (m_meshletsBuffer) m_commandList->SetGraphicsRootShaderResourceView(4, m_meshletsBuffer->GetGPUVirtualAddress());

        Transition(m_commandList.Get(), m_shadowMapTexture.Get(), m_shadowMapState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    gpuStamp(3);
    const bool rrRequested=m_dlssRRVersion!=DLSSRRVersion::Off&&m_dlssRRSupported&&m_upscaleTechnique==UpscaleTechnique::DLSS&&m_dxrEnabled&&m_dxrDevice&&m_rrPipelineState&&m_rrDiffuseAlbedo&&m_rrSpecularAlbedo&&m_normalTexture&&m_rrHitDistance;
    const bool lightingGBuffer=m_rrPipelineState&&(rrRequested||m_ssgiEnabled||m_ssaoEnabled);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[5];
    rtvHandles[0] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandles[0].ptr += m_sceneColorRtvIndex * m_rtvDescriptorSize;
    rtvHandles[1] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandles[1].ptr += m_velocityRtvIndex * m_rtvDescriptorSize;
    rtvHandles[2] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandles[2].ptr += m_normalRtvIndex * m_rtvDescriptorSize;
    rtvHandles[3]=m_rtvHeap->GetCPUDescriptorHandleForHeapStart();rtvHandles[3].ptr+=28*m_rtvDescriptorSize;rtvHandles[4]=m_rtvHeap->GetCPUDescriptorHandleForHeapStart();rtvHandles[4].ptr+=29*m_rtvDescriptorSize;

    float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Restore nice dark gray
    float velocityClear[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float normalClear[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    
    // Explicitly unbind all RTVs before clear/draw
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);

    LOG("Render: frame=" << m_frameIndex << " rtv[0].ptr=" << rtvHandles[0].ptr);
    
    LOG("Render: Transitioning G-Buffer resources");
    Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(m_commandList.Get(), m_normalTexture.Get(), m_normalState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_firstFrameAfterResize = false;
    if (m_framesAfterResize > 0) --m_framesAfterResize;

    LOG("Render: Clearing render targets");
    m_commandList->ClearRenderTargetView(rtvHandles[0], clearColor, 0, nullptr);
    m_commandList->ClearRenderTargetView(rtvHandles[1], velocityClear, 0, nullptr);
    m_commandList->ClearRenderTargetView(rtvHandles[2], normalClear, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    if(lightingGBuffer){Transition(m_commandList.Get(),m_rrDiffuseAlbedo.Get(),m_rrDiffuseState,D3D12_RESOURCE_STATE_RENDER_TARGET);Transition(m_commandList.Get(),m_rrSpecularAlbedo.Get(),m_rrSpecularState,D3D12_RESOURCE_STATE_RENDER_TARGET);const float black[4]={0,0,0,0};m_commandList->ClearRenderTargetView(rtvHandles[3],black,0,nullptr);m_commandList->ClearRenderTargetView(rtvHandles[4],black,0,nullptr);}
    m_commandList->OMSetRenderTargets(lightingGBuffer?5:3, rtvHandles, FALSE, &dsvHandle);

    D3D12_VIEWPORT mainViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
    D3D12_RECT mainScissor = { 0, 0, m_renderWidth, m_renderHeight };
    m_commandList->RSSetViewports(1, &mainViewport);
    m_commandList->RSSetScissorRects(1, &mainScissor);

    // --- Main Mesh Shader Rendering Pass ---
    m_commandList->SetPipelineState(lightingGBuffer?m_rrPipelineState.Get():m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* mainHeaps[] = { m_mainDescHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, mainHeaps);

    // Устанавливаем таблицу текстур (параметр 1)
    m_commandList->SetGraphicsRootDescriptorTable(1, m_mainDescHeap->GetGPUDescriptorHandleForHeapStart());

    // Устанавливаем общие ресурсы
    m_commandList->SetGraphicsRootShaderResourceView(2, m_vertexBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(3, m_indexBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(4, m_meshletsBuffer->GetGPUVirtualAddress());

    // Устанавливаем таблицу текстур Space 1 (параметр 5: t3-t10 space 1)
    D3D12_GPU_DESCRIPTOR_HANDLE space1GpuHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    space1GpuHandle.ptr += (UINT64)m_hizSrvIndex * m_cbvSrvDescriptorSize;
    m_commandList->SetGraphicsRootDescriptorTable(5, space1GpuHandle);

    XMMATRIX world;

    // Для каждой модели
    for (auto& model : m_models) {
        if (!model.visible) continue;
    
        // Обновляем world-матрицу модели
        XMMATRIX scale = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
        XMMATRIX rot = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(model.rotation.x),
            XMConvertToRadians(model.rotation.y),
            XMConvertToRadians(model.rotation.z)
        );
        XMMATRIX trans = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
        world = scale * rot * trans;
        XMStoreFloat4x4(&model.worldMatrix, world);

        // Helper to resolve texture index
        auto getMatIndex = [&](const std::shared_ptr<Texture>& tex, int fallbackIdx) -> UINT {
            if (tex && tex->GetState() == TextureState::Ready) return tex->GetSrvIndex();
            if (fallbackIdx >= 0) return (UINT)(fallbackIdx + 1);
            return 0;
        };

        // Form constants for this model
        SceneConstantBuffer cbData = m_constantBufferData;
        cbData.viewProj = m_viewProj;
        cbData.model = m_cullingProbeOutside?XMMatrixTranslation(100000,0,0):XMLoadFloat4x4(&model.worldMatrix);
        cbData.previousModel = model.previousTransformFrame && model.previousTransformFrame + 1 == m_frameCounter
            ? XMLoadFloat4x4(&model.previousWorldMatrix) : cbData.model;
        XMStoreFloat4x4(&model.previousWorldMatrix, cbData.model);
        model.previousTransformFrame = m_frameCounter;
        cbData.startIndexLocation = model.startIndexLocation;
        cbData.baseVertexLocation = model.baseVertexLocation;
        cbData.triangleCount = model.indexCount / 3;
        cbData.indexFormat = 1; // 32-bit indices
        cbData.albedoIndex = getMatIndex(model.material.albedoMap ? model.material.albedoMap : model.albedoMap, model.albedoIndex);
        if (cbData.albedoIndex == 0 && model.texture && model.texture->GetState() == TextureState::Ready) {
            cbData.albedoIndex = model.texture->GetSrvIndex();
        }
        cbData.normalIndex = getMatIndex(model.material.normalMap ? model.material.normalMap : model.normalMap, model.normalIndex);
        cbData.rmaIndex = getMatIndex(model.material.rmaMap ? model.material.rmaMap : model.rmaMap, model.rmaIndex);
        cbData.aoIndex = getMatIndex(model.material.aoMap, model.aoIndex);
        cbData.emissiveIndex = getMatIndex(model.material.emissiveMap, model.emissiveIndex);
        cbData.matAlbedo = model.material.albedo;
        cbData.matRoughness = model.material.roughness;
        cbData.matMetallic = model.material.metallic;
        cbData.matReflectance = model.material.reflectance;
        cbData.matEmission = model.material.emission;
        cbData.matIOR = model.material.ior;
        cbData.uvTiling = model.material.uvTiling;
        cbData.meshletStart = model.meshletStart;
        cbData.meshletCount = model.meshletCount;

        currentCbIndex++;
        UINT64 cbOffset = (UINT64)currentCbIndex * cbStride;
        if (m_pCbvDataBegin && cbOffset + sizeof(SceneConstantBuffer) <= 1024 * 1024) {
            memcpy(reinterpret_cast<UINT8*>(m_pCbvDataBegin) + cbOffset, &cbData, sizeof(SceneConstantBuffer));
        }

        if(EngineRenderTraceEnabled())LOG("Model: " << model.name << " visible=" << model.visible << " meshletStart=" << model.meshletStart << " meshletCount=" << model.meshletCount);
        m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + cbOffset);
        uint32_t asGroupCount = (model.meshletCount + 31) / 32;
        if (!DrawTessellated(model.material,model.indexCount,false,lightingGBuffer) && asGroupCount > 0) {
            DrawGeometry(asGroupCount,model.indexCount);
            if(EngineRenderTraceEnabled())LOG("Dispatched mesh for " << model.name << " with " << asGroupCount << " groups");
        }
    }

    // Dynamic models (m_dynamicModels)
    for (auto& model : m_dynamicModels) {
        if (!model.visible) continue;

        XMMATRIX scale = XMMatrixScaling(model.scale.x, model.scale.y, model.scale.z);
        XMMATRIX rot = XMMatrixRotationRollPitchYaw(
            XMConvertToRadians(model.rotation.x),
            XMConvertToRadians(model.rotation.y),
            XMConvertToRadians(model.rotation.z)
        );
        XMMATRIX trans = XMMatrixTranslation(model.position.x, model.position.y, model.position.z);
        world = scale * rot * trans;
        XMStoreFloat4x4(&model.worldMatrix, world);

        auto getMatIndex = [&](const std::shared_ptr<Texture>& tex, int fallbackIdx) -> UINT {
            if (tex && tex->GetState() == TextureState::Ready) return tex->GetSrvIndex();
            if (fallbackIdx >= 0) return (UINT)(fallbackIdx + 1);
            return 0;
        };

        SceneConstantBuffer cbData = m_constantBufferData;
        cbData.viewProj = m_viewProj;
        cbData.model = m_cullingProbeOutside?XMMatrixTranslation(100000,0,0):world;
        cbData.previousModel = model.previousTransformFrame && model.previousTransformFrame + 1 == m_frameCounter
            ? XMLoadFloat4x4(&model.previousWorldMatrix) : cbData.model;
        XMStoreFloat4x4(&model.previousWorldMatrix, cbData.model);
        model.previousTransformFrame = m_frameCounter;
        cbData.startIndexLocation = 0;
        cbData.baseVertexLocation = 0;
        cbData.triangleCount = model.indexCount / 3;
        cbData.indexFormat = 1;
        cbData.albedoIndex = getMatIndex(model.material.albedoMap ? model.material.albedoMap : model.albedoMap, model.albedoIndex);
        if (cbData.albedoIndex == 0 && model.texture && model.texture->GetState() == TextureState::Ready) {
            cbData.albedoIndex = model.texture->GetSrvIndex();
        }
        cbData.normalIndex = getMatIndex(model.material.normalMap ? model.material.normalMap : model.normalMap, model.normalIndex);
        cbData.rmaIndex = getMatIndex(model.material.rmaMap ? model.material.rmaMap : model.rmaMap, model.rmaIndex);
        cbData.aoIndex = getMatIndex(model.material.aoMap, model.aoIndex);
        cbData.emissiveIndex = getMatIndex(model.material.emissiveMap, model.emissiveIndex);
        cbData.matAlbedo = model.material.albedo;
        cbData.matRoughness = model.material.roughness;
        cbData.matMetallic = model.material.metallic;
        cbData.matReflectance = model.material.reflectance;
        cbData.matEmission = model.material.emission;
        cbData.matIOR = model.material.ior;
        cbData.uvTiling = model.material.uvTiling;
        cbData.meshletStart = 0;
        cbData.meshletCount = model.meshletCount;

        currentCbIndex++;
        UINT64 cbOffset = (UINT64)currentCbIndex * cbStride;
        if (m_pCbvDataBegin && cbOffset + sizeof(SceneConstantBuffer) <= 1024 * 1024) {
            memcpy(reinterpret_cast<UINT8*>(m_pCbvDataBegin) + cbOffset, &cbData, sizeof(SceneConstantBuffer));
        }

        m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + cbOffset);
        m_commandList->SetGraphicsRootShaderResourceView(2, model.vertexBuffer->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(3, model.indexBuffer->GetGPUVirtualAddress());
        m_commandList->SetGraphicsRootShaderResourceView(4, model.meshletBuffer->GetGPUVirtualAddress());

        uint32_t asGroupCount = (model.meshletCount + 31) / 32;
        if (!DrawTessellated(model.material,model.indexCount,false,lightingGBuffer) && asGroupCount > 0) {
            DrawGeometry(asGroupCount,model.indexCount);
        }
    }

    // Restore static buffer bindings
    if (m_vertexBuffer) m_commandList->SetGraphicsRootShaderResourceView(2, m_vertexBuffer->GetGPUVirtualAddress());
    if (m_indexBuffer) m_commandList->SetGraphicsRootShaderResourceView(3, m_indexBuffer->GetGPUVirtualAddress());
    if (m_meshletsBuffer) m_commandList->SetGraphicsRootShaderResourceView(4, m_meshletsBuffer->GetGPUVirtualAddress());

    BuildDepthPyramid();
    LOG("Render: VSM Profile Start");

    // --- Virtual Shadow Map (VSM) Management Pass ---
    if (IsVirtualTexturingIntegrated() && m_vsmPhysicalAtlas && m_vsmManagementPipelineState) {
        Transition(m_commandList.Get(), m_vsmPageTable.Get(), m_vsmPageTableState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ID3D12DescriptorHeap* vsmHeaps[] = { m_mainDescHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, vsmHeaps);
        
        m_commandList->SetComputeRootSignature(m_vsmManagementRootSignature.Get());
        m_commandList->SetPipelineState(m_vsmManagementPipelineState.Get());

        D3D12_GPU_DESCRIPTOR_HANDLE vsmSrvHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        vsmSrvHandle.ptr += (UINT64)m_depthSrvIndex * m_cbvSrvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE vsmUavHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        vsmUavHandle.ptr += (UINT64)m_vsmPageTableUavIndex * m_cbvSrvDescriptorSize;

        m_commandList->SetComputeRootDescriptorTable(0, vsmSrvHandle);
        m_commandList->SetComputeRootDescriptorTable(1, vsmUavHandle);
        
        struct VSMConstants {
            float ScreenRes[2];
            float AtlasRes[2];
            float SunMVP[16];
        } vsmConsts = {};
        vsmConsts.ScreenRes[0] = static_cast<float>(m_renderWidth);
        vsmConsts.ScreenRes[1] = static_cast<float>(m_renderHeight);
        vsmConsts.AtlasRes[0] = 8192.0f;
        vsmConsts.AtlasRes[1] = 8192.0f;
        XMMATRIX sunMVP = XMMatrixIdentity(); 
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(vsmConsts.SunMVP), sunMVP);

        m_commandList->SetComputeRoot32BitConstants(2, 20, &vsmConsts, 0);

        DispatchCompute("VSM / surface cache",m_commandList.Get(),512 / 16,512 / 16,1);

        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        Transition(m_commandList.Get(), m_vsmPageTable.Get(), m_vsmPageTableState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // Restore graphics pipeline state after compute passes (HiZ / etc.)
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipelineState.Get());
    {
        ID3D12DescriptorHeap* heaps[] = { m_mainDescHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, heaps);
    }
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Runtime Virtual Texturing Pass
    if (IsVirtualTexturingIntegrated() && m_rvtPhysicalAtlasAlbedo && m_rvtBakePipelineState) {
        LOG("Render: Runtime Virtual Texturing Pass");
        Transition(m_commandList.Get(), m_rvtPhysicalAtlasAlbedo.Get(), m_rvtPhysicalAtlasAlbedoState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(m_commandList.Get(), m_rvtPhysicalAtlasNormal.Get(), m_rvtPhysicalAtlasNormalState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        
        D3D12_CPU_DESCRIPTOR_HANDLE rvtRtvs[2];
        rvtRtvs[0] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rvtRtvs[0].ptr += m_rvtAlbedoAtlasRtvIndex * m_rtvDescriptorSize;
        rvtRtvs[1] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rvtRtvs[1].ptr += m_rvtNormalAtlasRtvIndex * m_rtvDescriptorSize;

        m_commandList->OMSetRenderTargets(2, rvtRtvs, FALSE, nullptr);
        
        float clearColor[] = { 0.5f, 0.5f, 0.5f, 1.0f };
        m_commandList->ClearRenderTargetView(rvtRtvs[0], clearColor, 0, nullptr);
        m_commandList->ClearRenderTargetView(rvtRtvs[1], clearColor, 0, nullptr);
        
        D3D12_VIEWPORT rvtViewport = { 0.0f, 0.0f, 8192.0f, 8192.0f, 0.0f, 1.0f };
        D3D12_RECT rvtScissor = { 0, 0, 8192, 8192 };
        m_commandList->RSSetViewports(1, &rvtViewport);
        m_commandList->RSSetScissorRects(1, &rvtScissor);

        m_commandList->SetGraphicsRootSignature(m_rvtBakeRootSignature.Get());
        m_commandList->SetPipelineState(m_rvtBakePipelineState.Get());

        // --- Render Decals into RVT ---
        if (m_rvtDecalPipelineState && !m_decals.empty()) {
            m_commandList->SetGraphicsRootSignature(m_rvtDecalRootSignature.Get());
            m_commandList->SetPipelineState(m_rvtDecalPipelineState.Get());

            D3D12_GPU_DESCRIPTOR_HANDLE texTable = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
            m_commandList->SetGraphicsRootDescriptorTable(1, texTable);

            for (auto& decal : m_decals) {
                if (!decal.active) continue;

                struct DecalConstants {
                    XMMATRIX WorldToDecal;
                    XMMATRIX PageTransform;
                    UINT TextureIndex;
                    XMFLOAT4 DecalColor;
                } constants;

                constants.WorldToDecal = XMMatrixInverse(nullptr, decal.worldMatrix);
                constants.PageTransform = XMMatrixIdentity(); 
                constants.TextureIndex = decal.textureIndex;
                constants.DecalColor = decal.color;

                m_commandList->SetGraphicsRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
                
                m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                m_commandList->DrawInstanced(36, 1, 0, 0); 
            }
        }

        Transition(m_commandList.Get(), m_rvtPhysicalAtlasAlbedo.Get(), m_rvtPhysicalAtlasAlbedoState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_rvtPhysicalAtlasNormal.Get(), m_rvtPhysicalAtlasNormalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // --- Astra Radiosity Solver ---
    if (IsVirtualTexturingIntegrated() && m_astraRadiosityPipelineState && m_tlas && m_rvtPhysicalAtlasIrradiance) {
        Transition(m_commandList.Get(), m_rvtPhysicalAtlasIrradiance.Get(), m_rvtPhysicalAtlasIrradianceState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_commandList->SetComputeRootSignature(m_astraRadiosityRootSignature.Get());
        m_commandList->SetPipelineState(m_astraRadiosityPipelineState.Get());

        m_commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
        m_commandList->SetComputeRootShaderResourceView(1, m_tlas->GetGPUVirtualAddress());

        D3D12_GPU_DESCRIPTOR_HANDLE rvtSrvTable = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        rvtSrvTable.ptr += (UINT64)m_rvtAlbedoAtlasSrvIndex * m_cbvSrvDescriptorSize;
        m_commandList->SetComputeRootDescriptorTable(2, rvtSrvTable);

        D3D12_GPU_DESCRIPTOR_HANDLE irrUav = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        irrUav.ptr += (UINT64)m_rvtIrradianceAtlasUavIndex * m_cbvSrvDescriptorSize;
        m_commandList->SetComputeRootDescriptorTable(3, irrUav);

        DispatchCompute("VSM / surface cache",m_commandList.Get(),512 / 16,512 / 16,1);

        Transition(m_commandList.Get(), m_rvtPhysicalAtlasIrradiance.Get(), m_rvtPhysicalAtlasIrradianceState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

        // Prototype Page Table Allocation: Map a 256x256 region to Page (0,0)
        // In a real system, this would be done via the management compute shader
        D3D12_BOX box = { 0, 0, 0, 256, 256, 1 };
        uint32_t pageValue = 0 | (0 << 16); // X=0, Y=0
        // (Simplified for now, real RVT uses a compute shader to update the page table)

    // Skybox pass
    if (m_skyboxPipelineState) {
        if (m_pSkyboxCbvDataBegin) {
            SkyboxConstantBuffer skyData;
            XMMATRIX invViewProj = XMMatrixInverse(nullptr, m_cleanViewProj);
            XMStoreFloat4x4(&skyData.invViewProj, invViewProj);
            skyData.lightColor = XMFLOAT4(m_constantBufferData.lightColor.x, m_constantBufferData.lightColor.y, m_constantBufferData.lightColor.z, m_cloudDensity);
            skyData.lightDir = XMFLOAT4(m_constantBufferData.lightDir.x, m_constantBufferData.lightDir.y, m_constantBufferData.lightDir.z, 0.0f);
            skyData.skyColor = XMFLOAT4(m_skyColor.x, m_skyColor.y, m_skyColor.z, m_constantBufferData.rainWetness);
            memcpy(m_pSkyboxCbvDataBegin, &skyData, sizeof(SkyboxConstantBuffer));
        }

        // Bind 2 render targets (sceneColor and velocity) matching skybox PSO
        D3D12_CPU_DESCRIPTOR_HANDLE skyboxRtvs[2];
        skyboxRtvs[0] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        skyboxRtvs[0].ptr += m_sceneColorRtvIndex * m_rtvDescriptorSize;
        skyboxRtvs[1] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        skyboxRtvs[1].ptr += m_velocityRtvIndex * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_commandList->OMSetRenderTargets(2, skyboxRtvs, FALSE, &dsvHandle);

        D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT scissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);

        m_commandList->SetPipelineState(m_skyboxPipelineState.Get());
        m_commandList->SetGraphicsRootSignature(m_skyboxRootSignature.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_skyboxConstantBuffer->GetGPUVirtualAddress());

        // Рисуем небо
        m_commandList->DrawInstanced(3, 1, 0, 0);
        LOG("Skybox: DrawInstanced done");
    }

    if (m_cloudGatherPipelineState) {
        // Переключаемся на рендер-таргет для облаков (mboitMomentsTexture)
        Transition(m_commandList.Get(), m_mboitMomentsTexture.Get(), m_mboitMomentsState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE momentsRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        momentsRtvHandle.ptr += m_mboitMomentsRtvIndex * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &momentsRtvHandle, FALSE, nullptr);
        const float clearZero[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_commandList->ClearRenderTargetView(momentsRtvHandle, clearZero, 0, nullptr);

        CloudConstants cloudData = {};
        float fov = XMConvertToRadians(m_fov);
        float aspect = (float)m_renderWidth / (float)m_renderHeight;
        XMMATRIX projMat = m_camera->GetProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
        XMMATRIX viewMat = m_camera->GetViewMatrix();
        cloudData.invViewProj = XMMatrixTranspose(XMMatrixInverse(nullptr, viewMat * projMat));
        cloudData.cameraPos = m_camera->GetPosition();
        cloudData.time = m_cloudTime;
        cloudData.lightDir = m_constantBufferData.lightDir;
        cloudData.cloudDensity = m_cloudDensity;
        if (m_pCloudCbvDataBegin) memcpy(m_pCloudCbvDataBegin, &cloudData, sizeof(CloudConstants));

        m_commandList->SetPipelineState(m_cloudGatherPipelineState.Get());
        m_commandList->SetGraphicsRootSignature(m_cloudGatherRootSignature.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_cloudConstantBuffer->GetGPUVirtualAddress());

        // Рисуем облака (gather)
        m_commandList->DrawInstanced(3, 1, 0, 0);

        // Возвращаем m_mboitMomentsTexture в состояние SRV
        Transition(m_commandList.Get(), m_mboitMomentsTexture.Get(), m_mboitMomentsState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    if (m_mboitMomentsTexture) {
        LOG("Render: Cloud Resolve Pass - mboitMomentsTexture OK");
        
        D3D12_VIEWPORT cloudViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT cloudScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &cloudViewport);
        m_commandList->RSSetScissorRects(1, &cloudScissor);

        D3D12_CPU_DESCRIPTOR_HANDLE targetRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        // Always render clouds to sceneColor - ray tracing will composite later
        targetRtvHandle.ptr += m_sceneColorRtvIndex * m_rtvDescriptorSize;
        
        m_commandList->OMSetRenderTargets(1, &targetRtvHandle, FALSE, nullptr);
        LOG("Render: Cloud Resolve - OMSetRenderTargets OK");

        D3D12_VIEWPORT cloudResolveViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT cloudResolveScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &cloudResolveViewport);
        m_commandList->RSSetScissorRects(1, &cloudResolveScissor);

        m_commandList->SetPipelineState(m_cloudResolvePipelineState.Get());
        LOG("Render: Cloud Resolve - SetPipelineState OK");
        m_commandList->SetGraphicsRootSignature(m_cloudResolveRootSignature.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->SetGraphicsRootConstantBufferView(0, m_cloudConstantBuffer->GetGPUVirtualAddress());
        LOG("Render: Cloud Resolve - Root CBV OK");

        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); // Depth as SRV for cloud resolve
        LOG("Render: Cloud Resolve - Depth transition OK");

        ID3D12DescriptorHeap* cloudResolveHeaps[] = { m_mainDescHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, cloudResolveHeaps);
        LOG("Render: Cloud Resolve - SetDescriptorHeaps OK");

        D3D12_GPU_DESCRIPTOR_HANDLE momentsSrvGpu = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        momentsSrvGpu.ptr += m_mboitMomentsSrvIndex * m_cbvSrvDescriptorSize;
        m_commandList->SetGraphicsRootDescriptorTable(1, momentsSrvGpu);

        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvGpu = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        depthSrvGpu.ptr += m_depthSrvIndex * m_cbvSrvDescriptorSize;
        m_commandList->SetGraphicsRootDescriptorTable(2, depthSrvGpu);
        LOG("Render: Cloud Resolve - DescriptorTables OK");

        m_commandList->DrawInstanced(3, 1, 0, 0);
        LOG("Render: Cloud Resolve - DrawInstanced OK");

        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_WRITE); // Depth back to write
        LOG("Render: Cloud Resolve - Depth back to write OK");
    } else {
        LOG("Render: Cloud Resolve Pass - SKIPPED (no mboitMomentsTexture)");
    }
    LOG("Render: Cloud Pass done");

    gpuStamp(4);
    LOG("Render: Before SSGI Pass check");
    const bool rasterGI=m_ssgiEnabled&&m_ssgiIntensity>0&&!(m_dxrEnabled&&m_dxrDevice);
    if (rasterGI && m_ssgiPipelineState && m_ssgiRootSignature) {
        LOG("Render: SSGI Pass");
        const auto giReadState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, giReadState);
        Transition(m_commandList.Get(), m_normalTexture.Get(), m_normalState, giReadState);
        Transition(m_commandList.Get(), m_rrDiffuseAlbedo.Get(), m_rrDiffuseState, giReadState);
        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_ssgiTexture.Get(), m_ssgiState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_commandList->SetPipelineState(m_ssgiPipelineState.Get());
        LOG("Render: SSGI - SetPipelineState OK");
        m_commandList->SetComputeRootSignature(m_ssgiRootSignature.Get());
        LOG("Render: SSGI - SetComputeRootSignature OK");

        SSGIConstantBuffer ssgiData = {};
        float fov = XMConvertToRadians(m_fov);
        float aspect = (float)m_renderWidth / (float)m_renderHeight;
        XMMATRIX projMat = m_camera->GetProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
        XMMATRIX viewMat = m_camera->GetViewMatrix();
        ssgiData.viewProj = XMMatrixTranspose(viewMat * projMat);
        ssgiData.invViewProj = XMMatrixTranspose(XMMatrixInverse(nullptr, viewMat * projMat));
        ssgiData.cameraPos = m_camera->GetPosition();
        ssgiData.rayStepSize = 0.15f;
        ssgiData.maxRayDistance = 12.0f;
        ssgiData.raySteps = 12;
        ssgiData.intensity = m_ssgiIntensity;
        
        if (m_pSsgiCbvDataBegin) memcpy(m_pSsgiCbvDataBegin, &ssgiData, sizeof(SSGIConstantBuffer));
        m_commandList->SetComputeRootConstantBufferView(2, m_ssgiConstantBuffer->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* ssgiHeaps[] = { m_ssgiSrvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, ssgiHeaps);
        
        D3D12_GPU_DESCRIPTOR_HANDLE ssgiSrvs = m_ssgiSrvHeap->GetGPUDescriptorHandleForHeapStart();
        m_commandList->SetComputeRootDescriptorTable(0, ssgiSrvs);

        D3D12_GPU_DESCRIPTOR_HANDLE ssgiUav = m_ssgiSrvHeap->GetGPUDescriptorHandleForHeapStart();
        UINT ssgiDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ssgiUav.ptr += 4 * ssgiDescSize; // Because SRVs take index 0,1,2
        m_commandList->SetComputeRootDescriptorTable(1, ssgiUav);

        UINT dispatchX = (m_renderWidth + 7) / 8;
        UINT dispatchY = (m_renderHeight + 7) / 8;
        DispatchCompute("SSGI",m_commandList.Get(),dispatchX,dispatchY,1);
        LOG("Render: SSGI - Dispatch OK");

        Transition(m_commandList.Get(), m_ssgiTexture.Get(), m_ssgiState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        LOG("Render: SSGI Pass done");
    } else {
        LOG("Render: SSGI Pass SKIPPED (no PSO or RootSig)");
    }

    gpuStamp(5);
    if (m_dxrEnabled && m_dxrDevice) {
        LOG("Render: DoRayTracing Pass");
        DoRayTracing();
    }

    gpuStamp(6);
    LOG("Render: Before Composite Pass");
    // Composite Pass: Combine SceneColor + SSAO + SSGI + DXR (if enabled)
    bool doComposite = m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice);
    LOG("Render: Composite condition: m_ssaoEnabled=" << m_ssaoEnabled << " m_dxrEnabled=" << m_dxrEnabled << " doComposite=" << doComposite);
    if (doComposite) {
        LOG("Render: Composite Pass - m_ssaoTexture=" << (void*)m_ssaoTexture.Get());
        
        if (m_ssaoTexture && m_ssaoEnabled) {
            LOG("Render: Composite Pass - Starting SSAO pass");
            Transition(m_commandList.Get(), m_ssaoTexture.Get(), m_ssaoState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            LOG("Render: SSAO transition OK");

            D3D12_VIEWPORT ssaoViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT ssaoScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &ssaoViewport);
        m_commandList->RSSetScissorRects(1, &ssaoScissor);

        D3D12_CPU_DESCRIPTOR_HANDLE ssaoRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        ssaoRtvHandle.ptr += m_ssaoRtvIndex * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &ssaoRtvHandle, FALSE, nullptr);
        const float clearWhite[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        m_commandList->ClearRenderTargetView(ssaoRtvHandle, clearWhite, 0, nullptr);

        float fov = XMConvertToRadians(m_fov);
        float aspect = (float)m_renderWidth / (float)m_renderHeight;
        XMMATRIX projMat = m_camera->GetProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
        XMMATRIX viewMat = m_camera->GetViewMatrix();
        XMMATRIX invProjMat = XMMatrixInverse(nullptr, projMat);

        SSAOConstants ssaoData = {};
        ssaoData.projection = XMMatrixTranspose(projMat);
        ssaoData.invProjection = XMMatrixTranspose(invProjMat);
        ssaoData.view = XMMatrixTranspose(viewMat);
        ssaoData.uvScale = XMFLOAT2(1.0f, 1.0f); 
        for (int i = 0; i < 32; i++) {
            float phi = 2.3999632f * (float)i; // Golden angle
            float cosTheta = 1.0f - (float)(i + 1) / 33.0f;
            float sinTheta = sqrtf((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
            float x = cosf(phi) * sinTheta;
            float y = sinf(phi) * sinTheta;
            float z = cosTheta;
            XMVECTOR sample = XMVectorSet(x, y, z, 0.0f);
            
            float scale = (float)(i + 1) / 32.0f;
            scale = 0.1f + scale * scale * 0.9f;
            sample = XMVectorScale(sample, scale);
            XMStoreFloat4(&ssaoData.samples[i], sample);
        }
        ssaoData.noiseScale = XMFLOAT4((float)m_renderWidth, (float)m_renderHeight, 1.0f / (float)m_renderWidth, 1.0f / (float)m_renderHeight);
        ssaoData.radius = m_ssaoRadius;
        ssaoData.bias = m_ssaoBias;
        ssaoData.intensity = m_ssaoIntensity;
        memcpy(m_pSSAOCbvDataBegin, &ssaoData, sizeof(SSAOConstants));

        m_commandList->SetPipelineState(m_ssaoPipelineState.Get());
        m_commandList->SetGraphicsRootSignature(m_ssaoRootSignature.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_ssaoConstantBuffer->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* ssaoHeaps[] = { m_ssaoSrvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, ssaoHeaps);
        m_commandList->SetGraphicsRootDescriptorTable(1, m_ssaoSrvHeap->GetGPUDescriptorHandleForHeapStart());

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        Transition(m_commandList.Get(), m_ssaoTexture.Get(), m_ssaoState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_ssaoBlurTexture.Get(), m_ssaoBlurState, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE blurRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        blurRtvHandle.ptr += m_ssaoBlurRtvIndex * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &blurRtvHandle, FALSE, nullptr);

        D3D12_VIEWPORT blurViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT blurScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &blurViewport);
        m_commandList->RSSetScissorRects(1, &blurScissor);

        m_commandList->ClearRenderTargetView(blurRtvHandle, clearWhite, 0, nullptr);

        SSAOBlurConstants blurData = {};
        blurData.texelSize = XMFLOAT2(1.0f / (float)m_renderWidth, 1.0f / (float)m_renderHeight);
        memcpy(m_pSSAOBlurCbvDataBegin, &blurData, sizeof(SSAOBlurConstants));

        m_commandList->SetPipelineState(m_ssaoBlurPipelineState.Get());
        m_commandList->SetGraphicsRootSignature(m_ssaoBlurRootSignature.Get());
        m_commandList->SetGraphicsRootConstantBufferView(0, m_ssaoBlurConstantBuffer->GetGPUVirtualAddress());

        ID3D12DescriptorHeap* blurHeaps[] = { m_ssaoBlurSrvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, blurHeaps);
        m_commandList->SetGraphicsRootDescriptorTable(1, m_ssaoBlurSrvHeap->GetGPUDescriptorHandleForHeapStart());

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        Transition(m_commandList.Get(), m_ssaoBlurTexture.Get(), m_ssaoBlurState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            LOG("Render: SSAO Pass done");
        } else if (m_ssaoEnabled) {
            LOG("Render: SSAO Pass SKIPPED - no m_ssaoTexture");
        }

        D3D12_VIEWPORT renderViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT renderScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &renderViewport);
        m_commandList->RSSetScissorRects(1, &renderScissor);

        Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_compositedColorTexture.Get(), m_compositedColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE compRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        compRtvHandle.ptr += m_compositedColorRtvIndex * m_rtvDescriptorSize;
        m_commandList->OMSetRenderTargets(1, &compRtvHandle, FALSE, nullptr);

        if (!m_compositePipelineState) {
            LOG("Render: ERROR - m_compositePipelineState is null!");
        } else {
            LOG("Render: Composite - PSO OK");
        }
        m_commandList->SetPipelineState(m_compositePipelineState.Get());
        if (!m_compositeRootSignature) {
            LOG("Render: ERROR - m_compositeRootSignature is null!");
        }
        LOG("Render: Composite - about to set root signature");
        m_commandList->SetGraphicsRootSignature(m_compositeRootSignature.Get());
        LOG("Render: Composite - root signature set OK");

        LOG("Render: Composite - m_mainDescHeap=" << (void*)m_mainDescHeap.Get());
        ID3D12DescriptorHeap* compHeaps[] = { m_mainDescHeap.Get() };
        LOG("Render: Composite - about to set descriptor heaps");
        m_commandList->SetDescriptorHeaps(1, compHeaps);
        LOG("Render: Composite - descriptor heaps OK");

        // Set CBV at slot 0 (b0 - composite constants if needed, or use dummy)
        LOG("Render: Composite - setting CBV at slot 0");
        SceneConstantBuffer compositeConstants=m_constantBufferData;
        compositeConstants.model=XMMatrixInverse(nullptr,m_viewProj);
        const UINT64 compositeOffset=UINT64(++currentCbIndex)*cbStride;
        if(compositeOffset+sizeof(compositeConstants)>1024*1024){LOG("Composite constant buffer exhausted");return;}
        memcpy(m_pCbvDataBegin+compositeOffset,&compositeConstants,sizeof(compositeConstants));
        m_commandList->SetGraphicsRootConstantBufferView(0,m_constantBuffer->GetGPUVirtualAddress()+compositeOffset);
        LOG("Render: Composite - CBV OK");
        
        // SRV descriptor tables at slots 1-4 (t0-t3)
        D3D12_GPU_DESCRIPTOR_HANDLE colorHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        colorHandle.ptr += m_sceneColorSrvIndex * m_cbvSrvDescriptorSize;
        LOG("Render: Composite - binding slot 1 (sceneColor, t0)");
        m_commandList->SetGraphicsRootDescriptorTable(1, colorHandle);
        LOG("Render: Composite - slot 1 OK");

        D3D12_GPU_DESCRIPTOR_HANDLE aoHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        aoHandle.ptr += (m_ssaoTexture && m_ssaoEnabled) ? (m_ssaoBlurSrvIndex * m_cbvSrvDescriptorSize) : (m_whiteSrvIndex * m_cbvSrvDescriptorSize);
        m_commandList->SetGraphicsRootDescriptorTable(2, aoHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE ssgiHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        ssgiHandle.ptr += (m_ssgiTexture && m_ssgiEnabled && m_ssgiIntensity>0 && (rasterGI||((m_dxrEnabled&&m_dxrDevice)&&!m_pathTracingEnabled))) ? (m_ssgiSrvIndex * m_cbvSrvDescriptorSize) : (m_nullSrvIndex * m_cbvSrvDescriptorSize);
        m_commandList->SetGraphicsRootDescriptorTable(3, ssgiHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE rtOutHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
        rtOutHandle.ptr += (m_dxrEnabled && m_dxrDevice && m_rayTracingOutput)
            ? (m_rayTracingOutputSrvIndex * m_cbvSrvDescriptorSize)
            : (m_nullSrvIndex * m_cbvSrvDescriptorSize);
        m_commandList->SetGraphicsRootDescriptorTable(4, rtOutHandle);
        Transition(m_commandList.Get(),m_normalTexture.Get(),m_normalState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(),m_rrDiffuseAlbedo.Get(),m_rrDiffuseState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(),m_depthStencilBuffer.Get(),m_depthStencilState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const UINT extraIndices[]={m_normalSrvIndex,m_depthSrvIndex,1510};
        for(UINT i=0;i<3;++i){auto handle=m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();handle.ptr+=UINT64(extraIndices[i])*m_cbvSrvDescriptorSize;m_commandList->SetGraphicsRootDescriptorTable(5+i,handle);}

        LOG("Render: Composite - about to draw");
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);
        LOG("Render: Composite - DrawInstanced OK");
        LOG("Render: Composite Pass - DrawInstanced OK");

        Transition(m_commandList.Get(), m_compositedColorTexture.Get(), m_compositedColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        LOG("Render: Composite Pass done");
    } else {
        LOG("Render: Composite Pass SKIPPED (doComposite=false)");
        Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    gpuStamp(7);
    // Rain rendering pass onto final composited/scene color
    if (rainSys.GetSettings().enabled) {
        bool hasComposited = doComposite && m_compositedColorTexture;
        ID3D12Resource* targetTex = hasComposited ? m_compositedColorTexture.Get() : m_sceneColorTexture.Get();
        D3D12_RESOURCE_STATES& targetState = hasComposited ? m_compositedColorState : m_sceneColorState;
        UINT targetRtvIdx = hasComposited ? m_compositedColorRtvIndex : m_sceneColorRtvIndex;

        Transition(m_commandList.Get(), targetTex, targetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_READ);

        D3D12_CPU_DESCRIPTOR_HANDLE rainRtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rainRtvHandle.ptr += targetRtvIdx * m_rtvDescriptorSize;
        D3D12_CPU_DESCRIPTOR_HANDLE rainDsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_commandList->OMSetRenderTargets(1, &rainRtvHandle, FALSE, &rainDsvHandle);

        D3D12_VIEWPORT rainViewport = { 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
        D3D12_RECT rainScissor = { 0, 0, m_renderWidth, m_renderHeight };
        m_commandList->RSSetViewports(1, &rainViewport);
        m_commandList->RSSetScissorRects(1, &rainScissor);

        rainSys.Render(m_commandList.Get(), m_viewProj, m_constantBufferData.lightColor, lightningFlash, m_camera->GetPosition(), m_constantBufferData.cloudTime, m_constantBufferData.lightDir);

        Transition(m_commandList.Get(), targetTex, targetState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Transparent particles use scene depth and the same HDR target as rain.
    auto& particles = EngineVFX::ParticleSystem::Get();
    if (particles.GetActiveEmitterCount() > 0) {
        const bool hasComposited = doComposite && m_compositedColorTexture;
        ID3D12Resource* target = hasComposited ? m_compositedColorTexture.Get() : m_sceneColorTexture.Get();
        D3D12_RESOURCE_STATES& state = hasComposited ? m_compositedColorState : m_sceneColorState;
        const UINT index = hasComposited ? m_compositedColorRtvIndex : m_sceneColorRtvIndex;
        Transition(m_commandList.Get(), target, state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_READ);
        auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += index * m_rtvDescriptorSize;
        auto dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        D3D12_VIEWPORT viewport = {0.f, 0.f, float(m_renderWidth), float(m_renderHeight), 0.f, 1.f};
        D3D12_RECT scissor = {0, 0, m_renderWidth, m_renderHeight};
        m_commandList->RSSetViewports(1, &viewport);
        m_commandList->RSSetScissorRects(1, &scissor);
        particles.Render(m_commandList.Get(), m_viewProj, m_camera->GetPosition(), m_constantBufferData.cloudTime);
        Transition(m_commandList.Get(), target, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    gpuStamp(8);
    m_dlssActive = false;
    bool upscalingSuccess = false;
    if (m_dlssSupported && m_upscaleTechnique == UpscaleTechnique::DLSS) {
        Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_normalTexture.Get(), m_normalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        
        if (m_dxrEnabled && m_dxrDevice) {
            Transition(m_commandList.Get(), m_rayTracingOutput.Get(), m_rayTracingOutputState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        if (m_ssaoEnabled && m_compositedColorTexture) {
            Transition(m_commandList.Get(), m_compositedColorTexture.Get(), m_compositedColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        Transition(m_commandList.Get(), m_upscaledColorTexture.Get(), m_upscaledColorState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);


        if (frameToken && m_width > 0 && m_height > 0 && m_framesAfterResize == 0) {
            sl::DLSSOptions dlssOpts = {};
            switch (m_upscaleQuality) {
                case UpscaleQuality::Quality: dlssOpts.mode = sl::DLSSMode::eMaxQuality; break;
                case UpscaleQuality::Balanced: dlssOpts.mode = sl::DLSSMode::eBalanced; break;
                case UpscaleQuality::Performance: dlssOpts.mode = sl::DLSSMode::eMaxPerformance; break;
                case UpscaleQuality::UltraPerformance: dlssOpts.mode = sl::DLSSMode::eUltraPerformance; break;
                case UpscaleQuality::Native: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
                default: dlssOpts.mode = sl::DLSSMode::eDLAA; break;
            }
            dlssOpts.outputWidth = m_width;
            dlssOpts.outputHeight = m_height;
            dlssOpts.sharpness = m_upscaleSharpness;
            dlssOpts.colorBuffersHDR = sl::Boolean::eTrue;

            // DLSS 4.5 AI Transformer Models (Preset K for Quality/Balanced/DLAA, Preset M/L for Performance)
            dlssOpts.dlaaPreset = sl::DLSSPreset::ePresetK;
            dlssOpts.qualityPreset = sl::DLSSPreset::ePresetK;
            dlssOpts.balancedPreset = sl::DLSSPreset::ePresetK;
            dlssOpts.performancePreset = sl::DLSSPreset::ePresetM;
            dlssOpts.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
            dlssOpts.ultraQualityPreset = sl::DLSSPreset::ePresetK;

            sl::Constants slConsts = {};
            XMFLOAT3 cPos = m_camera->GetPosition();
            XMFLOAT3 cUp = m_camera->GetUpVector();
            XMFLOAT3 cRight = m_camera->GetRightVector();
            XMFLOAT3 cFwd = m_camera->GetForwardVector();
            
            slConsts.cameraPos.x = cPos.x; slConsts.cameraPos.y = cPos.y; slConsts.cameraPos.z = cPos.z;
            slConsts.cameraUp.x = cUp.x; slConsts.cameraUp.y = cUp.y; slConsts.cameraUp.z = cUp.z;
            slConsts.cameraRight.x = cRight.x; slConsts.cameraRight.y = cRight.y; slConsts.cameraRight.z = cRight.z;
            slConsts.cameraFwd.x = cFwd.x; slConsts.cameraFwd.y = cFwd.y; slConsts.cameraFwd.z = cFwd.z;
            
            float fov = XMConvertToRadians(m_fov);
            float aspect = static_cast<float>(m_renderWidth) / m_renderHeight;
            XMMATRIX proj = m_camera->GetCleanProjectionMatrix(fov, aspect, 0.1f, 2000.0f);
            memcpy(&slConsts.cameraViewToClip, &proj, sizeof(float) * 16);

            // Use Streamline matrix helpers to calculate camera-centered matrices safely
            sl::recalculateCameraMatrices(slConsts);
            auto toPrevious=XMMatrixInverse(nullptr,m_cleanViewProj)*m_prevCleanViewProj;auto fromPrevious=XMMatrixInverse(nullptr,toPrevious);memcpy(&slConsts.clipToPrevClip,&toPrevious,64);memcpy(&slConsts.prevClipToClip,&fromPrevious,64);

            XMFLOAT2 jitter = m_camera->GetJitter();
            slConsts.jitterOffset.x = jitter.x * m_renderWidth * 0.5f;
            slConsts.jitterOffset.y = -jitter.y * m_renderHeight * 0.5f;
            slConsts.cameraPinholeOffset.x = 0; slConsts.cameraPinholeOffset.y = 0;
            slConsts.mvecScale.x = 1.0f;
            slConsts.mvecScale.y = 1.0f;

            slConsts.cameraNear = 0.1f;
            slConsts.cameraFar = 2000.0f;
            slConsts.cameraFOV = fov;
            slConsts.cameraAspectRatio = aspect;
            slConsts.depthInverted = sl::Boolean::eFalse;
            slConsts.cameraMotionIncluded = sl::Boolean::eTrue;
            slConsts.motionVectors3D = sl::Boolean::eFalse;
            slConsts.reset = (m_frameCounter <= 2 || m_dlssNeedsReset) ? sl::Boolean::eTrue : sl::Boolean::eFalse;

            sl::ViewportHandle vHandle = { 0 };
            bool hasComposited = ((m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice)) && m_compositedColorTexture);
            ID3D12Resource* resolvedInputColor = hasComposited ? m_compositedColorTexture.Get() : m_sceneColorTexture.Get();
            sl::Resource colorRes = { sl::ResourceType::eTex2d, resolvedInputColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
            sl::Resource depthRes = { sl::ResourceType::eTex2d, m_depthStencilBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
            sl::Resource mvecRes = { sl::ResourceType::eTex2d, m_velocityTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
            sl::Resource outputRes = { sl::ResourceType::eTex2d, m_upscaledColorTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
            sl::Extent inputExtent = {0, 0, (uint32_t)m_renderWidth, (uint32_t)m_renderHeight};
            sl::Extent outputExtent = {0, 0, (uint32_t)m_width, (uint32_t)m_height};
            sl::ResourceTag tags[] = {
                sl::ResourceTag(&colorRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent),
                sl::ResourceTag(&outputRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &outputExtent),
                sl::ResourceTag(&depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent),
                sl::ResourceTag(&mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &inputExtent)
            };
            slSetTagForFrame(*frameToken, vHandle, tags, _countof(tags), m_commandList.Get());
            if(rrRequested){
                Transition(m_commandList.Get(),m_rrDiffuseAlbedo.Get(),m_rrDiffuseState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);Transition(m_commandList.Get(),m_rrSpecularAlbedo.Get(),m_rrSpecularState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);Transition(m_commandList.Get(),m_normalTexture.Get(),m_normalState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);Transition(m_commandList.Get(),m_rrHitDistance.Get(),m_rrHitDistanceState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                sl::Resource diffuse{sl::ResourceType::eTex2d,m_rrDiffuseAlbedo.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},specular{sl::ResourceType::eTex2d,m_rrSpecularAlbedo.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},normal{sl::ResourceType::eTex2d,m_normalTexture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},distance{sl::ResourceType::eTex2d,m_rrHitDistance.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
                sl::ResourceTag guides[]={{&diffuse,sl::kBufferTypeAlbedo,sl::ResourceLifecycle::eValidUntilEvaluate,&inputExtent},{&specular,sl::kBufferTypeSpecularAlbedo,sl::ResourceLifecycle::eValidUntilEvaluate,&inputExtent},{&normal,sl::kBufferTypeNormalRoughness,sl::ResourceLifecycle::eValidUntilEvaluate,&inputExtent},{&distance,sl::kBufferTypeSpecularHitDistance,sl::ResourceLifecycle::eValidUntilEvaluate,&inputExtent}};slSetTagForFrame(*frameToken,vHandle,guides,4,m_commandList.Get());
            }
            slDLSSSetOptions(vHandle, dlssOpts);
            if (m_dlssRRSupported) {
                sl::DLSSDOptions dlssdOpts = {};
                dlssdOpts.mode = rrRequested ? dlssOpts.mode : sl::DLSSMode::eOff;
                dlssdOpts.outputWidth = (uint32_t)m_width;
                dlssdOpts.outputHeight = (uint32_t)m_height;
                dlssdOpts.sharpness = m_upscaleSharpness;
                dlssdOpts.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
                dlssdOpts.colorBuffersHDR = sl::Boolean::eTrue;

                // SDK model presets D/E.
                sl::DLSSDPreset rrPreset = (m_dlssRRVersion == DLSSRRVersion::Version1_0) ? sl::DLSSDPreset::ePresetD : sl::DLSSDPreset::ePresetE;
                dlssdOpts.dlaaPreset = rrPreset;
                dlssdOpts.qualityPreset = rrPreset;
                dlssdOpts.balancedPreset = rrPreset;
                dlssdOpts.performancePreset = rrPreset;
                dlssdOpts.ultraPerformancePreset = rrPreset;
                dlssdOpts.ultraQualityPreset = rrPreset;

                if (m_camera) {
                    XMMATRIX v = m_camera->GetViewMatrix();
                    XMVECTOR det;
                    XMMATRIX invV = XMMatrixInverse(&det, v);
                    XMFLOAT4X4 viewMat, invViewMat;
                    XMStoreFloat4x4(&viewMat, v);
                    XMStoreFloat4x4(&invViewMat, invV);

                    memcpy(&dlssdOpts.worldToCameraView, &viewMat, sizeof(sl::float4x4));
                    memcpy(&dlssdOpts.cameraViewToWorld, &invViewMat, sizeof(sl::float4x4));
                }

                slDLSSDSetOptions(vHandle, dlssdOpts);
            }
            slSetConstants(slConsts, *frameToken, vHandle);
            sl::Feature featureToEval = rrRequested ? sl::kFeatureDLSS_RR : sl::kFeatureDLSS;
            const sl::BaseStructure* inputs[] = { 
                static_cast<const sl::BaseStructure*>(&vHandle),
                static_cast<const sl::BaseStructure*>(&dlssOpts)
            };
            sl::Result res = slEvaluateFeature(featureToEval, *frameToken, inputs, rrRequested?1:_countof(inputs), m_commandList.Get());
            m_dlssRRActive=rrRequested&&res==sl::Result::eOk;
            if (res == sl::Result::eOk) {
                upscalingSuccess = true;
                m_dlssActive = true;
                m_dlssNeedsReset = false;
            } else {
                static int errorCount = 0;
                if (errorCount++ % 100 == 0) {
                    LOG("slEvaluateFeature FAILED with result: " << (int)res);
                }
                m_dlssActive = false;
            }
        }
    } else if (m_upscaleTechnique == UpscaleTechnique::FSR) {
        if (m_framesAfterResize == 0) {
            bool hasComposited = ((m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice)) && m_compositedColorTexture);
            ID3D12Resource* fsrInputColor = hasComposited ? m_compositedColorTexture.Get() : m_sceneColorTexture.Get();
            D3D12_RESOURCE_STATES& fsrInputState = hasComposited ? m_compositedColorState : m_sceneColorState;

            Transition(m_commandList.Get(), fsrInputColor, fsrInputState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Transition(m_commandList.Get(), m_upscaledColorTexture.Get(), m_upscaledColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);

            upscalingSuccess=ApplyFSR2(m_commandList.Get());

            Transition(m_commandList.Get(), m_upscaledColorTexture.Get(), m_upscaledColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

            upscalingSuccess = m_fsrActive;
        }
    } else if(m_upscaleTechnique==UpscaleTechnique::XeSS){
        ApplyXeSS(m_commandList.Get());upscalingSuccess=m_xessActive;
        Transition(m_commandList.Get(),m_upscaledColorTexture.Get(),m_upscaledColorState,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    } else {
        // Standalone TAA pass
        if (m_taaEnabled) {
            bool composite=(m_ssaoEnabled||m_ssgiEnabled||(m_dxrEnabled&&m_dxrDevice))&&m_compositedColorTexture;
            auto* color=composite?m_compositedColorTexture.Get():m_sceneColorTexture.Get();auto& state=composite?m_compositedColorState:m_sceneColorState;
            if(!m_taaHistoryValid){Transition(m_commandList.Get(),color,state,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(m_commandList.Get(),m_historyColorTexture.Get(),m_historyColorState,D3D12_RESOURCE_STATE_COPY_DEST);m_commandList->CopyResource(m_historyColorTexture.Get(),color);}
            Transition(m_commandList.Get(),color,state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(m_commandList.Get(),m_historyColorTexture.Get(),m_historyColorState,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);Transition(m_commandList.Get(),m_upscaledColorTexture.Get(),m_upscaledColorState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            
            ApplyTAA(m_commandList.Get());
            Transition(m_commandList.Get(),m_upscaledColorTexture.Get(),m_upscaledColorState,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(m_commandList.Get(),m_historyColorTexture.Get(),m_historyColorState,D3D12_RESOURCE_STATE_COPY_DEST);m_commandList->CopyResource(m_historyColorTexture.Get(),m_upscaledColorTexture.Get());m_taaHistoryValid=true;

            Transition(m_commandList.Get(), m_historyColorTexture.Get(), m_historyColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            upscalingSuccess = true;
        } else {
            LOG("Render: No upscale or TAA active");
        }
    }

    LOG("Render: Volumetric Fog Pass");
    gpuStamp(9);
    ApplyVolumetricFog(m_commandList.Get());

    LOG("Render: Post Process Pass");
    LOG("Render: PostProcess upscalingSuccess=" << upscalingSuccess << " dxrEnabled=" << m_dxrEnabled << " ssaoEnabled=" << m_ssaoEnabled);
    // Swapchain transition doesn't need tracking as it's a fixed Present -> RTV -> Present cycle
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        b.Transition.StateBefore = m_backBufferState;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &b);
        m_backBufferState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (upscalingSuccess) {
        Transition(m_commandList.Get(), m_upscaledColorTexture.Get(), m_upscaledColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    } else {
        if (m_dxrEnabled && m_dxrDevice) {
            Transition(m_commandList.Get(), m_rayTracingOutput.Get(), m_rayTracingOutputState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    D3D12_VIEWPORT fullViewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
    D3D12_RECT fullScissor = { 0, 0, m_width, m_height };
    m_commandList->RSSetViewports(1, &fullViewport);
    m_commandList->RSSetScissorRects(1, &fullScissor);

    D3D12_CPU_DESCRIPTOR_HANDLE backBufferHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    backBufferHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
    m_commandList->OMSetRenderTargets(1, &backBufferHandle, FALSE, nullptr);

    m_commandList->SetPipelineState(m_postProcessPipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_postProcessRootSignature.Get());

    ID3D12DescriptorHeap* ppPpHeaps[] = { m_mainDescHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppPpHeaps), ppPpHeaps);
    
    UINT descIncSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    D3D12_GPU_DESCRIPTOR_HANDLE colorSrvGpu = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();

    if (m_upscaleTechnique == UpscaleTechnique::DLSS && m_dlssActive && upscalingSuccess) {
        colorSrvGpu.ptr += m_upscaledColorSrvIndex * descIncSize;
        LOG("PostProcess: Using DLSS upscaled color (index=" << m_upscaledColorSrvIndex << ")");
    } else if (m_upscaleTechnique == UpscaleTechnique::FSR && upscalingSuccess) {
        colorSrvGpu.ptr += m_upscaledColorSrvIndex * descIncSize;
        LOG("PostProcess: Using FSR upscaled color (index=" << m_upscaledColorSrvIndex << ")");
    } else if(m_upscaleTechnique==UpscaleTechnique::XeSS&&upscalingSuccess){
        colorSrvGpu.ptr+=m_upscaledColorSrvIndex*descIncSize;
    } else if (m_taaEnabled && upscalingSuccess) {
        colorSrvGpu.ptr += m_historyColorSrvIndex * descIncSize;
        LOG("PostProcess: Using TAA history color (index=" << m_historyColorSrvIndex << ")");
    } else {
        // Fallback if upscaling/TAA is off - use composited color if available (includes DXR + SSAO + SSGI blending)
        if ((m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice)) && m_compositePipelineState) {
            colorSrvGpu.ptr += m_compositedColorSrvIndex * descIncSize;
            LOG("PostProcess: Using composited color (index=" << m_compositedColorSrvIndex << ")");
        } else {
            colorSrvGpu.ptr += m_sceneColorSrvIndex * descIncSize;
            LOG("PostProcess: Using scene color (index=" << m_sceneColorSrvIndex << ")");
        }
    }
    m_commandList->SetGraphicsRootDescriptorTable(0, colorSrvGpu);

    D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrvGpu = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    ssaoSrvGpu.ptr += m_ssaoBlurSrvIndex * descIncSize;
    m_commandList->SetGraphicsRootDescriptorTable(1, ssaoSrvGpu);
    auto ppConstants=EnginePostProcess::PostProcessSystem::Get().GetConstantBufferData();
    m_commandList->SetGraphicsRoot32BitConstants(2,sizeof(ppConstants)/4,&ppConstants,0);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    gpuStamp(10);
    PrepareTechnologyFrame(dt);
    Transition(m_commandList.Get(), m_sceneColorTexture.Get(), m_sceneColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(m_commandList.Get(), m_depthStencilBuffer.Get(), m_depthStencilState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    Transition(m_commandList.Get(), m_velocityTexture.Get(), m_velocityState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Transition(m_commandList.Get(), m_normalTexture.Get(), m_normalState, D3D12_RESOURCE_STATE_RENDER_TARGET);

    if (m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice)) {
        Transition(m_commandList.Get(), m_compositedColorTexture.Get(), m_compositedColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    if (upscalingSuccess) {
        Transition(m_commandList.Get(), m_upscaledColorTexture.Get(), m_upscaledColorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

#ifndef ENGINE_RELEASE
    if (m_imguiInitialized) {
        ImGui::Render();
        ID3D12DescriptorHeap* ppImGuiHeaps[] = { m_imguiSrvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, ppImGuiHeaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
    }
#endif

    TagDLSSGFrame(dt);
    gpuStamp(11);
    D3D12_RESOURCE_BARRIER presentBarrier = {};
    presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    presentBarrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    presentBarrier.Transition.StateBefore = m_backBufferState;
    presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &presentBarrier);
    m_backBufferState = D3D12_RESOURCE_STATE_PRESENT;

    if(m_queryHeap&&m_queryReadbackBuffer){m_commandList->EndQuery(m_queryHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);m_commandList->ResolveQueryData(m_queryHeap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,12,m_queryReadbackBuffer.Get(),0);m_hasGpuFrameTiming=true;}
    // Resolve Queries - DISABLED TEMPORARILY due to heap type issues
    // if (m_queryHeap && m_queryResultBuffer) {
    //     m_commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 8, m_queryResultBuffer.Get(), 0);
    //     D3D12_RESOURCE_BARRIER copyBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_queryResultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    //     m_commandList->ResourceBarrier(1, &copyBarrier);
    //     m_commandList->CopyResource(m_queryReadbackBuffer.Get(), m_queryResultBuffer.Get());
    //     D3D12_RESOURCE_BARRIER copyBarrierBack = CD3DX12_RESOURCE_BARRIER::Transition(m_queryResultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    //     m_commandList->ResourceBarrier(1, &copyBarrierBack);
    // }

    m_commandList->Close();

    // --- Extract and Log GPU Timings - DISABLED TEMPORARILY
    // static int profFrameCounter = 0;
    // if (++profFrameCounter % 300 == 0 && m_queryReadbackBuffer) {
    //     void* pData;
    //     D3D12_RANGE readRange = { 0, 8 * sizeof(UINT64) };
    //     if (SUCCEEDED(m_queryReadbackBuffer->Map(0, &readRange, &pData))) {
    //         UINT64* pTimestamps = reinterpret_cast<UINT64*>(pData);
    //         float vsmTime = (float)(pTimestamps[2] - pTimestamps[1]) / (float)m_timestampFrequency * 1000.0f;
    //         float rvtTime = (float)(pTimestamps[4] - pTimestamps[3]) / (float)m_timestampFrequency * 1000.0f;
    //         float naniteTime = (float)(pTimestamps[6] - pTimestamps[5]) / (float)m_timestampFrequency * 1000.0f;
    //         float totalTime = (float)(pTimestamps[7] - pTimestamps[0]) / (float)m_timestampFrequency * 1000.0f;
    //         m_queryReadbackBuffer->Unmap(0, nullptr);
    //         std::ofstream log("log.txt", std::ios::app);
    //         log << "[GPU Profile] VSM: " << std::fixed << std::setprecision(3) << vsmTime 
    //             << "ms | RVT: " << rvtTime 
    //             << "ms | Nanite: " << naniteTime 
    //             << "ms | Total: " << totalTime << "ms\n";
    //     }
    // }
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    MarkLatencyStage(2);
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
    MarkLatencyStage(3);

    UINT syncInterval = m_vsyncEnabled ? 1 : 0;
    BOOL isFullscreen = FALSE;
    if (m_swapChain) {
        m_swapChain->GetFullscreenState(&isFullscreen, nullptr);
    }
    UINT presentFlags = (m_tearingSupported && !m_vsyncEnabled && !isFullscreen) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    LOG("Present: syncInterval=" << syncInterval << " flags=0x" << std::hex << presentFlags << std::dec << " fullscreen=" << (isFullscreen ? "TRUE" : "FALSE"));

    if(m_xessFrameGenActive&&m_xessSystem)m_xessSystem->FGSetPresentId(uint32_t(m_frameCounter));
    MarkLatencyStage(4);
    auto presentStart=std::chrono::steady_clock::now();
    HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);
    EngineDebug::Profiler::Get().RecordSubsystemTime(EngineDebug::Subsystem::Present,std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-presentStart).count());
    MarkLatencyStage(5);FinishTechnologyPresent();

    // Reset backbuffer state to PRESENT after Present (swapchain returns buffers to PRESENT state)
    m_backBufferState = D3D12_RESOURCE_STATE_PRESENT;

    // Note: WaitForPreviousFrame moved to start of Render() to prevent
    // DXGI_ERROR_INVALID_CALL by ensuring backbuffer is free before rendering

    if (FAILED(hr)) {
        LOG("CRITICAL: Present failed with HRESULT: 0x" << std::hex << hr << std::dec << " syncInterval=" << syncInterval << " flags=0x" << std::hex << presentFlags << std::dec);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT reason = m_device->GetDeviceRemovedReason();
            LOG("Device Removed Reason: 0x" << std::hex << reason);

            // Set flag to prevent further rendering attempts
            m_deviceRemoved = true;

            // Show message box to inform user
            char msg[256];
            sprintf_s(msg, sizeof(msg), "D3D12 Device Removed!\n\nError Code: 0x%08X\n\nThe application will now exit to prevent further errors.", reason);
            MessageBoxA(nullptr, msg, "Device Removed Error", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
        }
    }

    LogD3D12Messages();
}

void Renderer::LogD3D12Messages() {
#if defined(_DEBUG)
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
        UINT64 messageCount = infoQueue->GetNumStoredMessages();
        if (messageCount > 0) {
            LOG("D3D12 Debug Messages (" << messageCount << "):");
            for (UINT64 i = 0; i < messageCount; ++i) {
                SIZE_T messageLength = 0;
                infoQueue->GetMessage(i, nullptr, &messageLength);
                D3D12_MESSAGE* pMessage = (D3D12_MESSAGE*)malloc(messageLength);
                if (pMessage) {
                    infoQueue->GetMessage(i, pMessage, &messageLength);
                    LOG(" [D3D12] " << pMessage->pDescription);
                    free(pMessage);
                }
            }
            infoQueue->ClearStoredMessages();
        }
    }

    typedef HRESULT(WINAPI* LPDXGIGETDEBUGINTERFACE)(REFIID, void**);
    HMODULE hDxgiDebug = GetModuleHandleW(L"dxgidebug.dll");
    if (!hDxgiDebug) hDxgiDebug = LoadLibraryW(L"dxgidebug.dll");
    if (hDxgiDebug) {
        LPDXGIGETDEBUGINTERFACE pfnDXGIGetDebugInterface = (LPDXGIGETDEBUGINTERFACE)GetProcAddress(hDxgiDebug, "DXGIGetDebugInterface");
        if (pfnDXGIGetDebugInterface) {
            ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
            if (SUCCEEDED(pfnDXGIGetDebugInterface(IID_PPV_ARGS(&dxgiInfoQueue)))) {
                static const GUID local_DXGI_DEBUG_ALL = { 0xe48ae283, 0xda52, 0x4a16, { 0x8e, 0x24, 0x0e, 0xc0, 0x24, 0x16, 0x9c, 0x23 } };
                UINT64 msgCount = dxgiInfoQueue->GetNumStoredMessages(local_DXGI_DEBUG_ALL);
                if (msgCount > 0) {
                    LOG("DXGI Debug Messages (" << msgCount << "):");
                    for (UINT64 i = 0; i < msgCount; i++) {
                        SIZE_T msgLen = 0;
                        dxgiInfoQueue->GetMessage(local_DXGI_DEBUG_ALL, i, nullptr, &msgLen);
                        DXGI_INFO_QUEUE_MESSAGE* msg = (DXGI_INFO_QUEUE_MESSAGE*)malloc(msgLen);
                        if (msg) {
                            if (SUCCEEDED(dxgiInfoQueue->GetMessage(local_DXGI_DEBUG_ALL, i, msg, &msgLen))) {
                                LOG(" [DXGI] " << msg->pDescription);
                            }
                            free(msg);
                        }
                    }
                    dxgiInfoQueue->ClearStoredMessages(local_DXGI_DEBUG_ALL);
                }
            }
        }
    }
#endif
}


void Renderer::BeginUI() {
#ifndef ENGINE_RELEASE
    if (m_imguiInitialized) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }
#endif
}

void Renderer::Destroy() {
    if(m_textureStreamer)m_textureStreamer->Shutdown();
    if(m_commandQueue&&m_fence)WaitForPreviousFrame();
    EngineVFX::ParticleSystem::Get().Shutdown();
    ReportBackendStatistics();
    m_pipelineCache.Shutdown();
    if(m_tessellatedDraws&&m_tessellationStatistics){
        D3D12_QUERY_DATA_PIPELINE_STATISTICS* stats=nullptr;
        D3D12_RANGE range{0,sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)};
        if(SUCCEEDED(m_tessellationStatistics->Map(0,&range,reinterpret_cast<void**>(&stats)))){
            LOG("Tessellation GPU: hull="<<stats->HSInvocations<<" domain="<<stats->DSInvocations<<" primitives="<<stats->IAPrimitives);
            D3D12_RANGE written{0,0};m_tessellationStatistics->Unmap(0,&written);
        }
        m_tessellationStatistics.Reset();m_tessellationQuery.Reset();
    }
    if(m_fidelityFXFrameGeneration)m_fidelityFXFrameGeneration->Disable();
    if(m_frameGenProxyProvider){for(auto& target:m_renderTargets)target.Reset();m_swapChain.Reset();}
    m_fidelityFXFrameGeneration.reset();
    ShutdownXeSS();ShutdownDirectStorage();
    if (m_streamlineInitialized) {
        slShutdown();
        m_streamlineInitialized=false;
    }
    
    ShutdownFSR2();

    if (m_dxrDevice) {
        m_dxrDevice.Reset();
        m_dxrCommandList.Reset();
        m_dxrStateObject.Reset();
        m_dxrGlobalRootSignature.Reset();
        m_tlas.Reset();
        m_tlasScratch.Reset();
        m_instanceDescs.Reset();
        m_rayTracingOutput.Reset();
        m_shaderTable.Reset();
    }

    WaitForPreviousFrame();

#ifndef ENGINE_RELEASE
    if (m_imguiInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }
#endif

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

static int s_spawnCounter = 1;

void Renderer::SpawnCube(const XMFLOAT3& pos) {
    char buf[32];
    do{sprintf_s(buf,"Cube %d",s_spawnCounter++);}while(std::any_of(m_models.begin(),m_models.end(),[&](const auto& model){return model.name==buf;}));
    RenderModel m(buf, m_cubeIndexCount, m_cubeStartIndex, m_cubeBaseVertex, m_cubeVertexCount);
    m.position = pos;
    m.texture = nullptr;
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}
void Renderer::SpawnPlane(const XMFLOAT3& pos) {
    char buf[32];
    do{sprintf_s(buf,"Plane %d",s_spawnCounter++);}while(std::any_of(m_models.begin(),m_models.end(),[&](const auto& model){return model.name==buf;}));
    RenderModel m(buf, m_planeIndexCount, m_planeStartIndex, m_planeBaseVertex, m_planeVertexCount);
    m.position = pos;
    m.texture = nullptr;
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}
void Renderer::SpawnSphere(const XMFLOAT3& pos) {
    char buf[32];
    do{sprintf_s(buf,"Sphere %d",s_spawnCounter++);}while(std::any_of(m_models.begin(),m_models.end(),[&](const auto& model){return model.name==buf;}));
    RenderModel m(buf, m_sphereIndexCount, m_sphereStartIndex, m_sphereBaseVertex, m_sphereVertexCount);
    m.position = pos;
    m.texture = nullptr;
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}
void Renderer::SpawnTriangle(const XMFLOAT3& pos) {
    char buf[32];
    do{sprintf_s(buf,"Triangle %d",s_spawnCounter++);}while(std::any_of(m_models.begin(),m_models.end(),[&](const auto& model){return model.name==buf;}));
    RenderModel m(buf, m_triangleIndexCount, m_triangleStartIndex, m_triangleBaseVertex, m_triangleVertexCount);
    m.position = pos;
    m.texture = nullptr;
    m_models.push_back(m);
    UpdateModelMeshletRanges();
}

void Renderer::SpawnDynamicRenderModel(const char* filepath, const XMFLOAT3& pos) {
    if (!m_device) return;

    std::vector<Renderer::Vertex> verts;
    std::vector<uint32_t> idx;

    std::filesystem::path fp(filepath);
    std::string ext = fp.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::wstring wpath = fp.wstring();
    size_t contentPos = wpath.find(L"Content");
    if (contentPos != std::wstring::npos) {
        wpath = wpath.substr(contentPos);
    }
    std::replace(wpath.begin(), wpath.end(), L'\\', L'/');

    auto pakData = LoadFromPak(wpath);
    bool loadSuccess = false;

    if (ext == ".obj") {
        tinyobj::ObjReaderConfig reader_config;
        reader_config.triangulate = true;
        tinyobj::ObjReader reader;

        bool parsed = false;
        if (!pakData.empty()) {
            std::string objStr(pakData.begin(), pakData.end());
            parsed = reader.ParseFromString(objStr, "");
        } else {
            parsed = reader.ParseFromFile(filepath, reader_config);
        }

        if (parsed) {
            const auto& attrib = reader.GetAttrib();
            const auto& shapes = reader.GetShapes();

            for (const auto& shape : shapes) {
                for (const auto& index : shape.mesh.indices) {
                    Renderer::Vertex v{};
                    v.position = {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 1],
                        attrib.vertices[3 * index.vertex_index + 2]
                    };
                    if (index.normal_index >= 0) {
                        v.normal = {
                            attrib.normals[3 * index.normal_index + 0],
                            attrib.normals[3 * index.normal_index + 1],
                            attrib.normals[3 * index.normal_index + 2]
                        };
                    } else {
                        v.normal = {0, 1, 0};
                    }
                    if (index.texcoord_index >= 0 && index.texcoord_index * 2 + 1 < (int)attrib.texcoords.size()) {
                        v.color = {1.0f, 1.0f, attrib.texcoords[2 * index.texcoord_index + 0], 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};
                    } else {
                        v.color = {1.0f, 1.0f, 0.0f, 0.0f};
                    }
                    verts.push_back(v);
                    idx.push_back((uint32_t)idx.size());
                }
            }
            loadSuccess = true;
        } else {
            LOG("Renderer: Failed to load OBJ (" << filepath << "): " << reader.Error());
        }
    } else if (ext == ".gltf" || ext == ".glb") {
        cgltf_options options = {};
        cgltf_data* data = NULL;
        cgltf_result result = cgltf_result_max_enum;
        
        if (!pakData.empty()) {
            result = cgltf_parse(&options, pakData.data(), pakData.size(), &data);
        } else {
            result = cgltf_parse_file(&options, filepath, &data);
        }

        if (result == cgltf_result_success) {
            cgltf_load_buffers(&options, data, filepath);
            for (size_t i = 0; i < data->meshes_count; ++i) {
                const cgltf_mesh& mesh = data->meshes[i];
                for (size_t p = 0; p < mesh.primitives_count; ++p) {
                    const cgltf_primitive& prim = mesh.primitives[p];
                    
                    size_t vCount = 0;
                    const float* posData = nullptr;
                    const float* normData = nullptr;
                    const float* uvData = nullptr;
                    
                    for (size_t a = 0; a < prim.attributes_count; ++a) {
                        const cgltf_attribute& attr = prim.attributes[a];
                        if (attr.type == cgltf_attribute_type_position) {
                            vCount = attr.data->count;
                            posData = (const float*)((const uint8_t*)attr.data->buffer_view->buffer->data + attr.data->buffer_view->offset + attr.data->offset);
                        } else if (attr.type == cgltf_attribute_type_normal) {
                            normData = (const float*)((const uint8_t*)attr.data->buffer_view->buffer->data + attr.data->buffer_view->offset + attr.data->offset);
                        } else if (attr.type == cgltf_attribute_type_texcoord) {
                            uvData = (const float*)((const uint8_t*)attr.data->buffer_view->buffer->data + attr.data->buffer_view->offset + attr.data->offset);
                        }
                    }

                    size_t startIndex = verts.size();
                    for (size_t v = 0; v < vCount; ++v) {
                        Renderer::Vertex vert{};
                        vert.position = {posData[v*3+0], posData[v*3+1], posData[v*3+2]};
                        if (normData) {
                            vert.normal = {normData[v*3+0], normData[v*3+1], normData[v*3+2]};
                        } else {
                            vert.normal = {0, 1, 0};
                        }
                        if (uvData) {
                            vert.color = {1.0f, 1.0f, uvData[v*2+0], uvData[v*2+1]};
                        } else {
                            vert.color = {1.0f, 1.0f, 0.0f, 0.0f};
                        }
                        verts.push_back(vert);
                    }

                    if (prim.indices) {
                        const cgltf_accessor* indAcc = prim.indices;
                        const uint8_t* indBuffer = (const uint8_t*)indAcc->buffer_view->buffer->data + indAcc->buffer_view->offset + indAcc->offset;
                        for (size_t id = 0; id < indAcc->count; ++id) {
                            if (indAcc->component_type == cgltf_component_type_r_16u) {
                                idx.push_back((uint32_t)((const uint16_t*)indBuffer)[id] + startIndex);
                            } else if (indAcc->component_type == cgltf_component_type_r_32u) {
                                idx.push_back(((const uint32_t*)indBuffer)[id] + startIndex);
                            }
                        }
                    } else {
                        for (size_t v = 0; v < vCount; ++v) {
                            idx.push_back((uint32_t)(startIndex + v));
                        }
                    }
                }
            }
            cgltf_free(data);
            loadSuccess = true;
        } else {
            LOG("Renderer: Failed to load GLTF/GLB (" << filepath << ")");
        }
    } else if (ext == ".fbx") {
        ufbx_load_opts opts = {};
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        ufbx_error error;
        ufbx_scene* scene = nullptr;
        
        if (!pakData.empty()) {
            scene = ufbx_load_memory(pakData.data(), pakData.size(), &opts, &error);
        } else {
            scene = ufbx_load_file(filepath, &opts, &error);
        }

        if (scene) {
            for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
                ufbx_mesh* mesh = scene->meshes.data[mi];
                size_t startVert = verts.size();
                size_t numTris = mesh->num_triangles;
                uint32_t* triIndices = (uint32_t*)malloc(mesh->max_face_triangles * 3 * sizeof(uint32_t));
                for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
                    ufbx_face face = mesh->faces.data[fi];
                    uint32_t numFaceTris = ufbx_triangulate_face(triIndices, mesh->max_face_triangles * 3, mesh, face);
                    for (uint32_t ti = 0; ti < numFaceTris * 3; ++ti) {
                        uint32_t index = triIndices[ti];
                        Renderer::Vertex v{};
                        ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                        v.position = { (float)pos.x, (float)pos.y, (float)pos.z };
                        if (mesh->vertex_normal.exists) {
                            ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                            v.normal = { (float)n.x, (float)n.y, (float)n.z };
                        } else {
                            v.normal = { 0, 1, 0 };
                        }
                        if (mesh->vertex_uv.exists) {
                            ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                            v.color = { 1.0f, 1.0f, (float)uv.x, 1.0f - (float)uv.y };
                        } else {
                            v.color = { 1.0f, 1.0f, 0.0f, 0.0f };
                        }
                        verts.push_back(v);
                        idx.push_back((uint32_t)(verts.size() - 1));
                    }
                }
                free(triIndices);
            }
            ufbx_free_scene(scene);
            loadSuccess = true;
        } else {
            MessageBoxA(nullptr, "Failed to load FBX", "Error", MB_OK);
        }
    } else {
        MessageBoxA(nullptr, "Unsupported model format (Only .obj, .gltf, .glb, .fbx supported)", "Error", MB_OK);
    }

    if (!loadSuccess || verts.empty() || idx.empty()) return;

    RenderDynamicModel dm;
    dm.name = fp.filename().string();
    {auto base=dm.name;unsigned suffix=2;while(std::any_of(m_dynamicModels.begin(),m_dynamicModels.end(),[&](const auto& model){return model.name==dm.name;}))dm.name=base+" ("+std::to_string(suffix++)+")";}
    dm.filepath = filepath;
    dm.indexCount = (UINT)idx.size();
    dm.position = pos;
    dm.texture = m_defaultTexture;

    const UINT vertexBufferSize = (UINT)(verts.size() * sizeof(Renderer::Vertex));
    const UINT indexBufferSize  = (UINT)(idx.size()  * sizeof(uint32_t));

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Alignment = 0;
    vbDesc.Width = vertexBufferSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.SampleDesc.Quality = 0;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.vertexBuffer)))) return;

    UINT8* pVertexDataBegin;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(dm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)))) {
        memcpy(pVertexDataBegin, verts.data(), vertexBufferSize);
        dm.vertexBuffer->Unmap(0, nullptr);
    }

    dm.vertexBufferView.BufferLocation = dm.vertexBuffer->GetGPUVirtualAddress();
    dm.vertexBufferView.StrideInBytes = sizeof(Renderer::Vertex);
    dm.vertexBufferView.SizeInBytes = vertexBufferSize;

    if (m_dxrDevice && !m_deviceRemoved) {
        // CreateBLAS/TLAS will be called in the next Render() frame via DoRayTracing
        // Calling them here during Initialize/LoadScene is unsafe as the command list might be closed.
    }

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = indexBufferSize;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.indexBuffer)))) return;

    UINT8* pIndexDataBegin;
    if (SUCCEEDED(dm.indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)))) {
        memcpy(pIndexDataBegin, idx.data(), indexBufferSize);
        dm.indexBuffer->Unmap(0, nullptr);
    }

    dm.indexBufferView.BufferLocation = dm.indexBuffer->GetGPUVirtualAddress();
    dm.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    dm.indexBufferView.SizeInBytes = indexBufferSize;

    std::vector<Renderer::Meshlet> meshlets;
    BuildMeshlets(verts, idx, meshlets);
    dm.meshletCount = (UINT)meshlets.size();
    dm.meshletBuffer = CreateStructuredBuffer(dm.meshletCount, sizeof(Renderer::Meshlet), meshlets.data());

    m_dynamicModels.push_back(std::move(dm));
    m_rtSceneDirty = true; // Trigger BLAS/TLAS rebuild next frame for the new model
}

uint32_t Renderer::CreateDynamicClothModel(const std::string& name, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, const PBRMaterial& material) {
    if (!m_device || vertices.empty() || indices.empty()) return 0;

    RenderDynamicModel dm;
    dm.name = name;
    dm.filepath = "";
    dm.indexCount = (UINT)indices.size();
    dm.startIndexLocation = 0;
    dm.baseVertexLocation = 0;
    dm.material = material;
    dm.modelId = RenderModel::NextId();
    dm.hasCollision = false; // Cloth and hair are soft bodies, do not block camera with rigid AABB
    dm.position = { 0.0f, 0.0f, 0.0f };
    dm.rotation = { 0.0f, 0.0f, 0.0f };
    dm.scale = { 1.0f, 1.0f, 1.0f };
    XMStoreFloat4x4(&dm.worldMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&dm.previousWorldMatrix, XMMatrixIdentity());

    UINT64 vertexBufferSize = (UINT64)vertices.size() * sizeof(Renderer::Vertex);
    UINT64 indexBufferSize = (UINT64)indices.size() * sizeof(uint32_t);

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Alignment = 0;
    vbDesc.Width = vertexBufferSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.SampleDesc.Quality = 0;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.vertexBuffer)))) {
        return 0;
    }

    UINT8* pVertexDataBegin = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (SUCCEEDED(dm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)))) {
        memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
        dm.vertexBuffer->Unmap(0, nullptr);
    }

    dm.vertexBufferView.BufferLocation = dm.vertexBuffer->GetGPUVirtualAddress();
    dm.vertexBufferView.StrideInBytes = sizeof(Renderer::Vertex);
    dm.vertexBufferView.SizeInBytes = (UINT)vertexBufferSize;

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = indexBufferSize;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dm.indexBuffer)))) {
        return 0;
    }

    UINT8* pIndexDataBegin = nullptr;
    if (SUCCEEDED(dm.indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)))) {
        memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
        dm.indexBuffer->Unmap(0, nullptr);
    }

    dm.indexBufferView.BufferLocation = dm.indexBuffer->GetGPUVirtualAddress();
    dm.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    dm.indexBufferView.SizeInBytes = (UINT)indexBufferSize;

    std::vector<Renderer::Meshlet> meshlets;
    BuildMeshlets(vertices, indices, meshlets);
    dm.meshletCount = (UINT)meshlets.size();
    dm.meshletBuffer = CreateStructuredBuffer(dm.meshletCount, sizeof(Renderer::Meshlet), meshlets.data());

    uint32_t createdId = dm.modelId;
    m_dynamicModels.push_back(std::move(dm));
    m_rtSceneDirty = true;
    return createdId;
}

bool Renderer::UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices) {
    for (auto& dm : m_dynamicModels) {
        if (dm.modelId == modelId) {
            if (!dm.vertexBuffer) return false;
            UINT64 byteSize = (UINT64)vertices.size() * sizeof(Renderer::Vertex);
            if (dm.vertexBuffer->GetDesc().Width < byteSize) {
                D3D12_RESOURCE_DESC vbDesc = dm.vertexBuffer->GetDesc();
                vbDesc.Width = byteSize;
                ComPtr<ID3D12Resource> newVb;
                if (FAILED(m_device->CreateCommittedResource(
                    &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &vbDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&newVb)))) {
                    return false;
                }
                if (dm.vertexBuffer) {
                    m_retiredResources.push_back(dm.vertexBuffer);
                }
                dm.vertexBuffer = newVb;
                dm.vertexBufferView.BufferLocation = dm.vertexBuffer->GetGPUVirtualAddress();
                dm.vertexBufferView.SizeInBytes = (UINT)byteSize;
            }

            UINT8* pData = nullptr;
            D3D12_RANGE readRange = { 0, 0 };
            if (SUCCEEDED(dm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)))) {
                memcpy(pData, vertices.data(), byteSize);
                dm.vertexBuffer->Unmap(0, nullptr);
                // The BLAS contains the previous vertex positions; rebuild only
                // this model before the next TLAS build after a deformation.
                if (dm.blas) m_retiredResources.push_back(dm.blas);
                if (dm.blasScratch) m_retiredResources.push_back(dm.blasScratch);
                dm.blas.Reset();
                dm.blasScratch.Reset();
                return true;
            }
            return false;
        }
    }
    return false;
}

bool Renderer::UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices) {
    for (auto& dm : m_dynamicModels) {
        if (dm.modelId == modelId) {
            UpdateDynamicModelVertexBuffer(modelId, vertices);
            UINT64 ibByteSize = (UINT64)indices.size() * sizeof(uint32_t);
            if (!dm.indexBuffer || dm.indexBuffer->GetDesc().Width < ibByteSize) {
                D3D12_RESOURCE_DESC ibDesc = {};
                ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                ibDesc.Width = ibByteSize;
                ibDesc.Height = 1;
                ibDesc.DepthOrArraySize = 1;
                ibDesc.MipLevels = 1;
                ibDesc.Format = DXGI_FORMAT_UNKNOWN;
                ibDesc.SampleDesc.Count = 1;
                ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                ComPtr<ID3D12Resource> newIb;
                if (FAILED(m_device->CreateCommittedResource(
                    &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &ibDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&newIb)))) {
                    return false;
                }
                if (dm.indexBuffer) {
                    m_retiredResources.push_back(dm.indexBuffer);
                }
                dm.indexBuffer = newIb;
                dm.indexBufferView.BufferLocation = dm.indexBuffer->GetGPUVirtualAddress();
                dm.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
                dm.indexBufferView.SizeInBytes = (UINT)ibByteSize;
            }

            UINT8* pData = nullptr;
            D3D12_RANGE readRange = { 0, 0 };
            if (SUCCEEDED(dm.indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)))) {
                memcpy(pData, indices.data(), ibByteSize);
                dm.indexBuffer->Unmap(0, nullptr);
            }
            dm.indexCount = (UINT)indices.size();

            std::vector<Renderer::Meshlet> meshlets;
            BuildMeshlets(vertices, indices, meshlets);
            dm.meshletCount = (UINT)meshlets.size();
            if (dm.meshletBuffer) {
                m_retiredResources.push_back(dm.meshletBuffer);
            }
            dm.meshletBuffer = CreateStructuredBuffer(dm.meshletCount, sizeof(Renderer::Meshlet), meshlets.data());
            m_rtSceneDirty = true;
            return true;
        }
    }
    return false;
}

bool Renderer::RemoveDynamicModel(uint32_t modelId) {
    for (auto it = m_dynamicModels.begin(); it != m_dynamicModels.end(); ++it) {
        if (it->modelId == modelId) {
            if (it->vertexBuffer) m_retiredResources.push_back(it->vertexBuffer);
            if (it->indexBuffer) m_retiredResources.push_back(it->indexBuffer);
            if (it->meshletBuffer) m_retiredResources.push_back(it->meshletBuffer);
            if (it->blas) m_retiredResources.push_back(it->blas);
            if (it->blasScratch) m_retiredResources.push_back(it->blasScratch);
            m_dynamicModels.erase(it);
            m_rtSceneDirty = true;
            return true;
        }
    }
    return false;
}

void Renderer::WaitForPreviousFrame() {
    if(!m_commandQueue||!m_fence||!m_fenceEvent)return;
    const UINT64 fence = m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), fence);
    m_fenceValue++;

    if (m_fence->GetCompletedValue() < fence) {
        LOG("WFPF: Waiting for event " << fence);
        m_fence->SetEventOnCompletion(fence, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
        LOG("WFPF: Wait complete");
    }

    m_retiredResources.clear();
    if(m_swapChain)m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

HRESULT Renderer::CreateRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc, ID3D12RootSignature** ppRootSignature) {
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(pDesc, &signature, &error);
    if (FAILED(hr)) {
        if (error) {
            LOG("Root Signature Serialization Failed: " << (char*)error->GetBufferPointer());
        }
        return hr;
    }
    hr=m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(ppRootSignature));
    if(SUCCEEDED(hr))m_pipelineCache.RegisterRoot(*ppRootSignature,signature->GetBufferPointer(),signature->GetBufferSize());
    return hr;
}





void Renderer::AssignMeshletRange(UINT startIdx, UINT indexCount, const std::vector<Meshlet>& meshlets, UINT& outStart, UINT& outCount) {
    outStart = 0;
    outCount = 0;
    const UINT rangeEnd = startIdx + indexCount;
    for (UINT i = 0; i < meshlets.size(); ++i) {
        const Meshlet& m = meshlets[i];
        if (m.PrimitiveCount == 0) continue;
        const UINT meshletEnd = m.PrimitiveOffset + m.PrimitiveCount * 3;
        if (startIdx < meshletEnd && rangeEnd > m.PrimitiveOffset) {
            if (outCount == 0) outStart = i;
            outCount++;
        }
    }
    if (outCount == 0) {
        outStart = 0;
        outCount = 1;
    }
}

void Renderer::UpdateModelMeshletRanges() {
    m_cpuMeshlets.clear();
    const uint32_t maxPrimitives = 42; // 42 triangles per meshlet

    for (auto& model : m_models) {
        if (model.indexCount == 0) {
            model.meshletStart = 0;
            model.meshletCount = 0;
            continue;
        }

        model.meshletStart = (UINT)m_cpuMeshlets.size();
        model.meshletCount = 0;

        for (uint32_t i = 0; i < model.indexCount; i += maxPrimitives * 3) {
            Renderer::Meshlet m = {};
            m.PrimitiveOffset = model.startIndexLocation + i;
            m.PrimitiveCount = (std::min)(maxPrimitives, (uint32_t)(model.indexCount - i) / 3);
            m.LODError = 0.0f;
            m.MaxParentError = 0.1f;
            m.ParentID = 0xFFFFFFFF;
            m.ConeCutoff = 1.0f;
            m.ConeNormal = { 0.0f, 1.0f, 0.0f };

            // Calculate bounding center & radius
            XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };
            float count = 0.0f;
            for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
                for (int k = 0; k < 3; ++k) {
                    if (m.PrimitiveOffset + p * 3 + k < m_staticIndices.size()) {
                        uint32_t localIdx = m_staticIndices[m.PrimitiveOffset + p * 3 + k];
                        uint32_t globalV = (uint32_t)(model.baseVertexLocation + localIdx);
                        if (globalV < m_staticVertices.size()) {
                            center.x += m_staticVertices[globalV].position.x;
                            center.y += m_staticVertices[globalV].position.y;
                            center.z += m_staticVertices[globalV].position.z;
                            count += 1.0f;
                        }
                    }
                }
            }
            if (count > 0.0f) {
                center.x /= count; center.y /= count; center.z /= count;
            }
            m.Center[0] = center.x; m.Center[1] = center.y; m.Center[2] = center.z;

            float maxDistSq = 0.0f;
            for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
                for (int k = 0; k < 3; ++k) {
                    if (m.PrimitiveOffset + p * 3 + k < m_staticIndices.size()) {
                        uint32_t localIdx = m_staticIndices[m.PrimitiveOffset + p * 3 + k];
                        uint32_t globalV = (uint32_t)(model.baseVertexLocation + localIdx);
                        if (globalV < m_staticVertices.size()) {
                            float dx = m_staticVertices[globalV].position.x - center.x;
                            float dy = m_staticVertices[globalV].position.y - center.y;
                            float dz = m_staticVertices[globalV].position.z - center.z;
                            maxDistSq = (std::max)(maxDistSq, dx*dx + dy*dy + dz*dz);
                        }
                    }
                }
            }
            m.Radius = std::sqrt(maxDistSq);

            m_cpuMeshlets.push_back(m);
            model.meshletCount++;
        }
    }

    m_meshletCount = (uint32_t)m_cpuMeshlets.size();
    if (m_meshletCount > 0) {
        if (m_meshletsBuffer) m_retiredResources.push_back(m_meshletsBuffer);
        m_meshletsBuffer = CreateStructuredBuffer(m_meshletCount, sizeof(Renderer::Meshlet), m_cpuMeshlets.data());
    }
    m_rtSceneDirty = true;
}

void Renderer::BuildMeshlets(const std::vector<Renderer::Vertex>& vertices, const std::vector<uint32_t>& indices, std::vector<Renderer::Meshlet>& outMeshlets) {
    const uint32_t maxPrimitives = 42; // 42 triangles * 3 = 126 vertices (optimal packing for 128-thread GPU mesh shader)
    uint32_t leafCount = 0;

    // --- Level 0: Leaf Clusters ---
    for (uint32_t i = 0; i < indices.size(); i += maxPrimitives * 3) {
        Renderer::Meshlet m = {};
        m.PrimitiveOffset = i;
        m.PrimitiveCount = (std::min)(maxPrimitives, (uint32_t)(indices.size() - i) / 3);
        m.LODError = 0.0f; 
        m.MaxParentError = 0.1f; 
        
        XMFLOAT3 center = {0,0,0};
        float count = 0;
        for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
            for (int k = 0; k < 3; ++k) {
                uint32_t idx = indices[i + p * 3 + k];
                center.x += vertices[idx].position.x;
                center.y += vertices[idx].position.y;
                center.z += vertices[idx].position.z;
                count += 1.0f;
            }
        }
        if (count > 0) {
            center.x /= count; center.y /= count; center.z /= count;
        }
        m.Center[0] = center.x; m.Center[1] = center.y; m.Center[2] = center.z;

        float maxDistSq = 0;
        for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
            for (int k = 0; k < 3; ++k) {
                uint32_t idx = indices[i + p * 3 + k];
                float dx = vertices[idx].position.x - center.x;
                float dy = vertices[idx].position.y - center.y;
                float dz = vertices[idx].position.z - center.z;
                maxDistSq = (std::max)(maxDistSq, dx*dx + dy*dy + dz*dz);
            }
        }
        m.Radius = sqrtf(maxDistSq);
        m.ParentID = 0xFFFFFFFF; 

        // Calculate Normal Cone
        XMVECTOR avgNormal = XMVectorSet(0,0,0,0);
        for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
            uint32_t i0 = indices[m.PrimitiveOffset + p * 3 + 0];
            uint32_t i1 = indices[m.PrimitiveOffset + p * 3 + 1];
            uint32_t i2 = indices[m.PrimitiveOffset + p * 3 + 2];
            XMVECTOR v0 = XMLoadFloat3(&vertices[i0].position);
            XMVECTOR v1 = XMLoadFloat3(&vertices[i1].position);
            XMVECTOR v2 = XMLoadFloat3(&vertices[i2].position);
            XMVECTOR n = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));
            avgNormal += n;
        }
        avgNormal = XMVector3Normalize(avgNormal);
        XMStoreFloat3(&m.ConeNormal, avgNormal);

        float minDot = 1.0f;
        for (uint32_t p = 0; p < m.PrimitiveCount; ++p) {
            uint32_t i0 = indices[m.PrimitiveOffset + p * 3 + 0];
            uint32_t i1 = indices[m.PrimitiveOffset + p * 3 + 1];
            uint32_t i2 = indices[m.PrimitiveOffset + p * 3 + 2];
            XMVECTOR v0 = XMLoadFloat3(&vertices[i0].position);
            XMVECTOR v1 = XMLoadFloat3(&vertices[i1].position);
            XMVECTOR v2 = XMLoadFloat3(&vertices[i2].position);
            XMVECTOR n = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));
            float d; XMStoreFloat(&d, XMVector3Dot(avgNormal, n));
            minDot = (std::min)(minDot, d);
        }
        m.ConeCutoff = minDot;

        leafCount++;
        outMeshlets.push_back(m);
    }

}

ComPtr<ID3D12Resource> Renderer::CreateStructuredBuffer(uint32_t count, uint32_t stride, const void* data) {
    UINT64 size = (UINT64)count * stride;
    ComPtr<ID3D12Resource> buffer;
    
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(
        &m_heapPropsUpload, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)))) {
        return nullptr;
    }

    if (data) {
        void* pMapped = nullptr;
        D3D12_RANGE readRange = {0,0};
        if (SUCCEEDED(buffer->Map(0, &readRange, &pMapped))) {
            memcpy(pMapped, data, size);
            buffer->Unmap(0, nullptr);
        }
    }
    return buffer;
}



void Renderer::ApplyReflections(ID3D12GraphicsCommandList* commandList) {
    if (!m_ssrPipelineState) return;

    // Transition reflection texture to UAV
    D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(m_reflectionTexture.Get(), m_reflectionTextureState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &b);
    m_reflectionTextureState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // HiZ texture should already be in NON_PIXEL_SHADER_RESOURCE state from pyramid generation

    commandList->SetComputeRootSignature(m_ssrRootSignature.Get());
    commandList->SetPipelineState(m_ssrPipelineState.Get());

    commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    
    D3D12_GPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    
    // Bind HiZ Buffer (Table 1)
    D3D12_GPU_DESCRIPTOR_HANDLE srvTable = mainHeapHandle;
    srvTable.ptr += (UINT64)m_hizSrvIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(1, srvTable);

    // Bind Output Texture (Table 2)
    D3D12_GPU_DESCRIPTOR_HANDLE uavTable = mainHeapHandle;
    uavTable.ptr += (UINT64)m_reflectionUavIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(2, uavTable);

    DispatchCompute("SSR",commandList,(m_renderWidth+15)/16,(m_renderHeight+15)/16,1);

    // Transition back to SRV
    b = CD3DX12_RESOURCE_BARRIER::Transition(m_reflectionTexture.Get(), m_reflectionTextureState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &b);
    m_reflectionTextureState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void Renderer::ApplyVolumetricFog(ID3D12GraphicsCommandList* commandList) {
    if (!m_volumetricFogEnabled || !m_volumetricFogInjectPipelineState) return;
    // Guard against null resources (creation may have failed during Resize)
    if (!m_volumetricFogVolume || !m_volumetricFogAccumulation) {
        LOG("ApplyVolumetricFog: skipped - fog resources are null");
        return;
    }
    if (!m_volumetricFogRootSignature || !m_volumetricFogAccumulatePipelineState) return;

    // Ensure the correct descriptor heap is bound for compute descriptor tables
    ID3D12DescriptorHeap* fogHeaps[] = { m_mainDescHeap.Get() };
    commandList->SetDescriptorHeaps(1, fogHeaps);

    // Transition volumes to UAV state
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_volumetricFogVolume.Get(), m_volumetricFogVolumeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_volumetricFogAccumulation.Get(), m_volumetricFogAccumulationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(2, barriers);
    m_volumetricFogVolumeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_volumetricFogAccumulationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    commandList->SetComputeRootSignature(m_volumetricFogRootSignature.Get());

    // 1. Injection Pass
    commandList->SetPipelineState(m_volumetricFogInjectPipelineState.Get());
    commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    // Bind Astra GI Irradiance Atlas SRV via descriptor table (t0)
    D3D12_GPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE irradianceSrv = mainHeapHandle;
    irradianceSrv.ptr += m_rvtIrradianceAtlasSrvIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(1, irradianceSrv);

    // Bind UAVs via descriptor table (u0, u1)
    D3D12_GPU_DESCRIPTOR_HANDLE uavTable = mainHeapHandle;
    uavTable.ptr += m_volumetricFogUavIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(2, uavTable);

    DispatchCompute("Fog inject",commandList,(160+7)/8,(90+7)/8,128); // Grid dimensions: 160x90x128 with numthreads(8,8,1)

    // Wait for injection
    D3D12_RESOURCE_BARRIER uavB = CD3DX12_RESOURCE_BARRIER::UAV(m_volumetricFogVolume.Get());
    commandList->ResourceBarrier(1, &uavB);

    // 2. Accumulation Pass
    commandList->SetPipelineState(m_volumetricFogAccumulatePipelineState.Get());
    DispatchCompute("Fog accumulate",commandList,(160+7)/8,(90+7)/8,1); // Internal loop handles Z

    // Transition back to SRV for pixel shader sampling
    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_volumetricFogVolume.Get(), m_volumetricFogVolumeState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_volumetricFogAccumulation.Get(), m_volumetricFogAccumulationState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(2, barriers);
    m_volumetricFogVolumeState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_volumetricFogAccumulationState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void Renderer::ApplyTAA(ID3D12GraphicsCommandList* commandList) {
    if (!m_taaPipelineState) return;

    ID3D12DescriptorHeap* heaps[] = { m_mainDescHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->SetComputeRootSignature(m_taaRootSignature.Get());
    commandList->SetPipelineState(m_taaPipelineState.Get());

    // Bind Constants
    commandList->SetComputeRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());

    // Bind SRVs via descriptor tables
    D3D12_GPU_DESCRIPTOR_HANDLE mainHeapHandle = m_mainDescHeap->GetGPUDescriptorHandleForHeapStart();

    // Bind current color (t0) via descriptor table
    bool hasComposited = ((m_ssaoEnabled || m_ssgiEnabled || (m_dxrEnabled && m_dxrDevice)) && m_compositedColorTexture);
    UINT colorSrvIdx = hasComposited ? m_compositedColorSrvIndex : m_sceneColorSrvIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE colorSrv = mainHeapHandle;
    colorSrv.ptr += colorSrvIdx * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(1, colorSrv);

    // Bind motion vectors (t1) via descriptor table
    D3D12_GPU_DESCRIPTOR_HANDLE velocitySrv = mainHeapHandle;
    velocitySrv.ptr += m_velocitySrvIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(2, velocitySrv);

    // Bind History UAV (u0) via descriptor table - slot 3
    D3D12_GPU_DESCRIPTOR_HANDLE hUav = mainHeapHandle;
    hUav.ptr += m_historyColorUavIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(3, hUav);

    // Bind History SRV (t2) for bilinear read - slot 4
    D3D12_GPU_DESCRIPTOR_HANDLE hHistorySrv = mainHeapHandle;
    hHistorySrv.ptr += m_historyColorSrvIndex * m_cbvSrvDescriptorSize;
    commandList->SetComputeRootDescriptorTable(4, hHistorySrv);

    DispatchCompute("TAA",commandList,(m_renderWidth+7)/8,(m_renderHeight+7)/8,1);
}

void Renderer::SpawnDecal(XMVECTOR position, XMVECTOR normal, float scale, UINT textureIndex) {
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);
    if (abs(XMVectorGetY(XMVector3Dot(normal, up))) > 0.99f) up = XMVectorSet(1, 0, 0, 0);
    
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, normal));
    up = XMVector3Normalize(XMVector3Cross(normal, right));
    
    XMMATRIX world = XMMatrixIdentity();
    world.r[0] = right * scale;
    world.r[1] = up * scale;
    world.r[2] = normal * scale;
    world.r[3] = XMVectorSetW(position, 1.0f);
    
    Decal decal;
    decal.worldMatrix = world;
    decal.textureIndex = textureIndex;
    decal.color = XMFLOAT4(1, 1, 1, 1);
    decal.active = true;
    
    m_decals.push_back(decal);
}





bool Renderer::IsDirectStorageSupported() const {
#if defined(_WIN32)
    return m_directStorage && m_directStorage->IsSupported();
#else
    return false;
#endif
}







static bool g_rendererInteractiveErrors = true;
void Renderer::SetInteractiveErrors(bool enabled) {
    g_rendererInteractiveErrors = enabled;
}

bool Renderer::GetCollisionMesh(uint32_t modelId, bool dynamic, EnginePhysics::CollisionMeshData& mesh) const {
    if (dynamic) {
        for (const auto& dm : m_dynamicModels) {
            if (dm.modelId == modelId) {
                mesh = dm.collisionMesh;
                return !mesh.vertices.empty();
            }
        }
    } else {
        for (const auto& sm : m_models) {
            if (sm.modelId == modelId) {
                mesh.vertices.clear();
                mesh.indices.clear();
                if (sm.indexCount == 0 || m_staticIndices.empty()) return false;
                
                mesh.indices.reserve(sm.indexCount);
                for (UINT i = 0; i < sm.indexCount; ++i) {
                    UINT idx = sm.startIndexLocation + i;
                    if (idx < m_staticIndices.size()) {
                        uint32_t localV = m_staticIndices[idx];
                        uint32_t globalV = (uint32_t)(sm.baseVertexLocation + localV);
                        if (globalV < m_staticVertices.size()) {
                            mesh.vertices.push_back(m_staticVertices[globalV].position);
                            mesh.indices.push_back((uint32_t)mesh.vertices.size() - 1);
                        }
                    }
                }
                return !mesh.vertices.empty();
            }
        }
    }
    return false;
}

bool Renderer::SaveScreenshotBMP(const std::string& path) {
    if (!m_device || !m_commandQueue || !m_swapChain) return false;
    const UINT renderedIndex=m_frameIndex;
    WaitForPreviousFrame();
    WriteTechnologyState(path);
    
    UINT currentIdx = renderedIndex;
    ID3D12Resource* srcRes = m_renderTargets[currentIdx].Get();
    if (!srcRes) return false;

    D3D12_RESOURCE_DESC desc = srcRes->GetDesc();
    UINT64 totalBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalBytes);

    ComPtr<ID3D12Resource> readback;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rbDesc = {};
    rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbDesc.Width = totalBytes;
    rbDesc.Height = 1;
    rbDesc.DepthOrArraySize = 1;
    rbDesc.MipLevels = 1;
    rbDesc.Format = DXGI_FORMAT_UNKNOWN;
    rbDesc.SampleDesc.Count = 1;
    rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
        return false;
    }

    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)))) {
        return false;
    }
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList)))) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(srcRes, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    CD3DX12_TEXTURE_COPY_LOCATION dstLoc(readback.Get(), layout);
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc(srcRes, 0);
    cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(srcRes, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    m_commandQueue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);

    void* pData = nullptr;
    D3D12_RANGE range = { 0, totalBytes };
    if (FAILED(readback->Map(0, &range, &pData))) {
        return false;
    }

    uint32_t width = (uint32_t)desc.Width;
    uint32_t height = desc.Height;
    uint32_t rowPitch = layout.Footprint.RowPitch;

    #pragma pack(push, 1)
    struct BMPHeader {
        uint16_t fileType{0x4D42};
        uint32_t fileSize{0};
        uint16_t reserved1{0};
        uint16_t reserved2{0};
        uint32_t offsetData{54};
        uint32_t size{40};
        int32_t width{0};
        int32_t height{0};
        uint16_t planes{1};
        uint16_t bitCount{24};
        uint32_t compression{0};
        uint32_t sizeImage{0};
        int32_t xPixelsPerMeter{0};
        int32_t yPixelsPerMeter{0};
        uint32_t colorsUsed{0};
        uint32_t colorsImportant{0};
    };
    #pragma pack(pop)

    uint32_t bmpRowPitch = (width * 3 + 3) & ~3;
    uint32_t dataSize = bmpRowPitch * height;
    BMPHeader hdr;
    hdr.fileSize = 54 + dataSize;
    hdr.width = (int32_t)width;
    hdr.height = (int32_t)height;
    hdr.sizeImage = dataSize;

    std::vector<uint8_t> bmpData(dataSize, 0);
    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(pData);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = srcBytes + (height - 1 - y) * rowPitch;
        uint8_t* dstRow = bmpData.data() + y * bmpRowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // R
        }
    }
    readback->Unmap(0, nullptr);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.write(reinterpret_cast<const char*>(bmpData.data()), bmpData.size());
    out.close();
    return true;
}

#include "RendererTechnologies.inl"
#endif // _WIN32

uint64_t Renderer::GetEditorTextureId(const std::shared_ptr<Texture>& texture){
#if defined(_WIN32) && !defined(ENGINE_RELEASE)
    if(!texture||texture->GetState()!=TextureState::Ready||!texture->GetResource()||!m_imguiSrvHeap)return 0;
    auto it=std::find(m_editorPreviewTextures.begin(),m_editorPreviewTextures.end(),texture);
    if(it==m_editorPreviewTextures.end()){
        if(m_editorPreviewTextures.size()>=1023)return 0;
        m_editorPreviewTextures.push_back(texture);it=m_editorPreviewTextures.end()-1;
    }
    UINT slot=UINT(it-m_editorPreviewTextures.begin())+1;
    UINT stride=m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto cpu=m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(slot)*stride;
    auto desc=texture->GetResource()->GetDesc();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=desc.MipLevels;
    m_device->CreateShaderResourceView(texture->GetResource(),&srv,cpu);
    auto gpu=m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=UINT64(slot)*stride;return gpu.ptr;
#else
    return 0;
#endif
}
