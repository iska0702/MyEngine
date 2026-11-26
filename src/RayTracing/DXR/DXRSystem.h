#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <cstdint>
#include "DxrTransform.h"

namespace EngineRender {

class DXRSystem {
public:
    DXRSystem();
    ~DXRSystem();

    bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* commandList);
    void Shutdown();

    bool IsSupported() const { return m_supported; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled && m_supported; }

    D3D12_RAYTRACING_TIER GetTier() const { return m_tier; }
    ID3D12Device5* GetDevice() const { return m_dxrDevice.Get(); }
    ID3D12GraphicsCommandList4* GetCommandList() const { return m_dxrCommandList.Get(); }

    void SetCommandList(ID3D12GraphicsCommandList* commandList);

    // Acceleration structure helpers
    bool GetBLASPrebuildInfo(const D3D12_RAYTRACING_GEOMETRY_DESC* geomDescs,
                             UINT numDescs,
                             D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outInfo);

    bool GetTLASPrebuildInfo(UINT numInstances,
                             D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& outInfo);

    void BuildAccelerationStructure(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc);

private:
    Microsoft::WRL::ComPtr<ID3D12Device5> m_dxrDevice;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;
    D3D12_RAYTRACING_TIER m_tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    bool m_supported = false;
    bool m_enabled = true;
};

} // namespace EngineRender
