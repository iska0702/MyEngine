#include "DXRSystem.h"
#include "Core/EngineLog.h"

namespace EngineRender {

DXRSystem::DXRSystem() = default;

DXRSystem::~DXRSystem() {
    Shutdown();
}

bool DXRSystem::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {
    if (!device) return false;

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        m_tier = options5.RaytracingTier;
        m_supported = (m_tier >= D3D12_RAYTRACING_TIER_1_0);
    } else {
        m_tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        m_supported = false;
    }

    if (!m_supported) {
        EngineLog::Write("DXRSystem: DirectX Raytracing is NOT supported on this hardware/driver.");
        return false;
    }

    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice));
    if (FAILED(hr)) {
        EngineLog::Write("DXRSystem: Failed to query ID3D12Device5 interface.");
        m_supported = false;
        return false;
    }

    if (commandList) {
        SetCommandList(commandList);
    }

    EngineLog::Write(std::string("DXRSystem: Initialized successfully with Raytracing Tier ") +
                     (m_tier == D3D12_RAYTRACING_TIER_1_1 ? "1.1" : "1.0"));
    return true;
}

void DXRSystem::Shutdown() {
    m_dxrCommandList.Reset();
    m_dxrDevice.Reset();
    m_supported = false;
    m_enabled = false;
    m_tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

void DXRSystem::SetCommandList(ID3D12GraphicsCommandList* commandList) {
    if (!commandList) {
        m_dxrCommandList.Reset();
        return;
    }
    commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList));
}

bool DXRSystem::GetBLASPrebuildInfo(const D3D12_RAYTRACING_GEOMETRY_DESC* geomDescs,
                                   UINT numDescs,
                                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outInfo) {
    if (!m_dxrDevice || !geomDescs || numDescs == 0) return false;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = geomDescs;
    inputs.NumDescs = numDescs;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &outInfo);
    return true;
}

bool DXRSystem::GetTLASPrebuildInfo(UINT numInstances,
                                   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outInfo) {
    if (!m_dxrDevice || numInstances == 0) return false;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = numInstances;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &outInfo);
    return true;
}

void DXRSystem::BuildAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc) {
    if (m_dxrCommandList) {
        m_dxrCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }
}

} // namespace EngineRender
