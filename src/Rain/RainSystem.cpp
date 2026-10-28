#ifndef NOMINMAX
#if defined(_WIN32)
#include "Renderer/PipelineStateCache.h"
#endif

#define NOMINMAX
#endif

#include "RainSystem.h"
#include "RainVisibility.h"
#if defined(_WIN32)
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace EngineRain {

namespace {
    const char* g_RainShaderCode = R"(
        cbuffer RainCB : register(b0) {
            float4x4 g_ViewProj;
            float4 g_LightColorAndIntensity; // rgb = light, a = lightning
            float4 g_CameraPosAndTime;       // xyz = camPos, w = time
            float4 g_LightDir;               // xyz = lightDir, w = unused
        };

        struct VSInput {
            float3 position : POSITION;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        struct PSInput {
            float4 position : SV_POSITION;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
            float3 worldPos : TEXCOORD1;
        };

        PSInput VSStreak(VSInput input) {
            PSInput output;
            output.position = mul(float4(input.position, 1.0f), g_ViewProj);
            output.uv = input.uv;
            output.color = input.color;
            output.worldPos = input.position;
            return output;
        }

        float4 PSStreak(PSInput input) : SV_TARGET {
            // 1. Aerodynamic cylindrical teardrop cross-section
            float xDist = (input.uv.x - 0.5f) * 2.0f;
            float r2 = xDist * xDist;
            if (r2 > 1.0f) discard;

            // Optical thickness of water cylinder (Fresnel / Snell's law refraction highlight)
            float waterThickness = sqrt(1.0f - r2);
            float coreGlow = pow(waterThickness, 1.8f);
            float edgeSoftness = smoothstep(1.0f, 0.2f, abs(xDist));

            // 2. Vertical streak profile (teardrop head at top uv.y=1, long tapered motion-blur tail at uv.y=0)
            float yProfile = pow(input.uv.y, 0.85f);
            float headBulb = smoothstep(0.75f, 1.0f, input.uv.y) * 0.25f;
            float alpha = (coreGlow * 0.40f + edgeSoftness * 0.25f + headBulb) * yProfile * input.color.a * 0.55f;

            // 3. Distance / Near-plane fade (prevent camera clipping artifacts)
            float distToCam = length(input.worldPos - g_CameraPosAndTime.xyz);
            alpha *= smoothstep(0.20f, 0.6f, distToCam);

            // 4. Directional Sun & Sky Lighting with Forward Mie Scattering
            float3 lightDir = normalize(g_LightDir.xyz);
            float3 viewDir = normalize(g_CameraPosAndTime.xyz - input.worldPos);
            float forwardScatter = pow(saturate(dot(viewDir, -lightDir)), 4.0f) * 1.2f;

            float3 sunGlow = g_LightColorAndIntensity.rgb * (0.65f + forwardScatter);
            float3 skyAmbient = float3(0.55f, 0.65f, 0.78f) * 0.75f;
            float3 lightningGlow = float3(1.6f, 1.7f, 2.0f) * (g_LightColorAndIntensity.a * 2.2f);

            float3 dropRgb = (sunGlow + skyAmbient + lightningGlow) * (0.75f + coreGlow * 0.35f);

            return float4(dropRgb, saturate(alpha));
        }

        PSInput VSSplash(VSInput input) {
            PSInput output;
            output.position = mul(float4(input.position, 1.0f), g_ViewProj);
            output.uv = input.uv;
            output.color = input.color;
            output.worldPos = input.position;
            return output;
        }

        float4 PSSplash(PSInput input) : SV_TARGET {
            float particleType = input.color.r;
            float lifeProgress = input.color.g;
            float customAlpha = input.color.a;

            float3 lightDir = normalize(g_LightDir.xyz);
            float3 viewDir = normalize(g_CameraPosAndTime.xyz - input.worldPos);

            // =========================================================================
            // TYPE 0: Expanding Floor Water Ripple Wave
            // =========================================================================
            if (particleType < 0.5f) {
                float2 uv = input.uv * 2.0f - 1.0f;
                float dist = length(uv);
                if (dist > 1.0f) discard;

                // Propagating circular wavefront (expanding from center outwards)
                float rWave = 0.08f + 0.86f * pow(saturate(lifeProgress), 0.65f);
                float ringWidth = 0.080f;

                // Primary circular wave crest (Gaussian wavefront)
                float delta1 = (dist - rWave) / ringWidth;
                float ring1 = exp(-delta1 * delta1);

                // Secondary trailing capillary ring (phase-shifted behind the main crest)
                float rWave2 = rWave - 0.18f;
                float ring2 = 0.0f;
                if (rWave2 > 0.04f) {
                    float delta2 = (dist - rWave2) / 0.060f;
                    ring2 = exp(-delta2 * delta2) * 0.30f;
                }

                // Wave amplitude decays via 2D circumference expansion (1/sqrt(r)) and surface tension / viscosity
                float waveDamping = pow(saturate(1.0f - lifeProgress), 1.5f) * (0.50f / sqrt(0.30f + 0.70f * lifeProgress));
                float edgeFade = saturate((1.0f - dist) * 5.0f);
                
                float waveHeight = (ring1 + ring2) * waveDamping * edgeFade;
                if (waveHeight < 0.003f) discard;

                // Dynamic water surface normal from wave radial gradient
                float dRing1 = -2.0f * delta1 * (1.0f / ringWidth) * ring1;
                float dRing2 = (rWave2 > 0.04f) ? (-2.0f * ((dist - rWave2) / 0.060f) * (1.0f / 0.060f) * ring2) : 0.0f;
                float dWave = (dRing1 + dRing2) * waveDamping * 0.030f;
                float2 grad = (dist > 0.001f) ? (uv / dist) * dWave : float2(0.0f, 0.0f);

                float3 Nwave = normalize(float3(-grad.x * 2.0f, 1.0f, -grad.y * 2.0f));

                // Physically-based water optics & specular reflections
                float NdotV = saturate(dot(Nwave, viewDir));
                // Water dielectric Fresnel (n = 1.333, F0 ~ 0.02)
                float fresnel = 0.02f + 0.98f * pow(1.0f - NdotV, 5.0f);

                // Subtle specular sunlight glint along the ripple crest
                float3 halfVec = normalize(viewDir + lightDir);
                float specSun = pow(saturate(dot(Nwave, halfVec)), 32.0f);
                float3 sunHighlight = g_LightColorAndIntensity.rgb * (specSun * 1.1f);

                // Subtle sky ambient reflection (soft and atmospheric, blending into dark wet floor)
                float3 skyReflection = lerp(float3(0.20f, 0.26f, 0.35f), float3(0.45f, 0.55f, 0.68f), saturate(Nwave.y)) * (0.35f + 0.40f * fresnel);
                
                // Lightning flash ambient
                float3 lightning = float3(1.2f, 1.3f, 1.6f) * (g_LightColorAndIntensity.a * 1.5f);

                float3 rippleRgb = sunHighlight + skyReflection + lightning;

                // Soft translucent water alpha (subtle, delicate, blends seamlessly into the floor)
                float finalAlpha = waveHeight * (0.16f + 0.30f * fresnel + specSun * 0.25f) * customAlpha;

                return float4(rippleRgb, saturate(finalAlpha));
            }
            // =========================================================================
            // TYPE 1: Leaping Micro-Droplet Beads
            // =========================================================================
            else if (particleType < 1.5f) {
                float2 uv = input.uv * 2.0f - 1.0f;
                float r2 = dot(uv, uv);
                if (r2 > 1.0f) discard;

                // Spherical water droplet profile
                float thickness = sqrt(1.0f - r2);
                float3 Nbead = float3(uv.x, uv.y, thickness);

                // Off-center specular highlight
                float3 halfVec = normalize(viewDir + lightDir);
                float spec = pow(saturate(dot(Nbead, float3(0.35f, 0.55f, 0.75f))), 20.0f);
                float fresnel = pow(1.0f - thickness, 3.0f) * 0.5f;
                float forwardScatter = pow(saturate(dot(viewDir, -lightDir)), 4.0f) * 1.0f;

                float3 sunGlow = g_LightColorAndIntensity.rgb * (spec * 1.1f + forwardScatter * 0.5f + 0.35f);
                float3 skyAmbient = float3(0.40f, 0.50f, 0.65f) * 0.5f;
                float3 lightning = float3(1.2f, 1.3f, 1.6f) * (g_LightColorAndIntensity.a * 1.8f);

                float3 beadRgb = (sunGlow + skyAmbient + lightning) * (0.8f + spec * 0.5f);
                float beadAlpha = (thickness * 0.18f + fresnel * 0.25f + spec * 0.35f) * saturate((1.0f - r2) * 4.0f) * customAlpha;

                return float4(beadRgb, saturate(beadAlpha));
            }
            // =========================================================================
            // TYPE 2: Vertical Splash Crown / Impact Burst
            // =========================================================================
            else {
                float2 uv = input.uv * 2.0f - 1.0f; // x in [-1, 1]
                float yNorm = input.uv.y;           // y in [0, 1]
                float xNorm = uv.x;
                
                // Conical splash crown profile
                float coneWidth = 0.20f + yNorm * 0.80f;
                float xDist = abs(xNorm) / max(coneWidth, 0.05f);
                if (xDist > 1.0f || yNorm < 0.02f || yNorm > 0.98f) discard;

                // Crown rim droplets / spikes (radial wave pattern)
                float spikePattern = sin(xNorm * 18.0f + yNorm * 10.0f) * 0.5f + 0.5f;
                float crownRim = smoothstep(0.40f, 0.95f, yNorm) * pow(spikePattern, 3.0f);
                
                // Thin translucent water sheet along the cone edges
                float sheetEdge = exp(-pow((xDist - 0.75f) / 0.20f, 2.0f));
                float centerMist = exp(-pow(xDist / 0.40f, 2.0f)) * (1.0f - yNorm) * 0.30f;

                float crownShape = sheetEdge * 0.6f + crownRim * 0.8f + centerMist;
                float fadeLife = pow(saturate(1.0f - lifeProgress), 1.8f);
                float edgeFade = saturate((1.0f - xDist) * 3.0f) * saturate(yNorm * 6.0f) * saturate((1.0f - yNorm) * 4.0f);

                float fwd = pow(saturate(dot(viewDir, -lightDir)), 3.0f) * 1.2f;

                float3 sunGlow = g_LightColorAndIntensity.rgb * (0.6f + fwd * 0.6f + crownRim * 0.7f);
                float3 skyAmbient = float3(0.40f, 0.50f, 0.65f) * 0.5f;
                float3 lightning = float3(1.2f, 1.3f, 1.6f) * (g_LightColorAndIntensity.a * 2.0f);

                float3 splashRgb = (sunGlow + skyAmbient + lightning);
                float splashAlpha = crownShape * fadeLife * edgeFade * (0.22f + crownRim * 0.35f) * customAlpha;

                return float4(splashRgb, saturate(splashAlpha));
            }
        }
    )";
}

RainSystem& RainSystem::Get() {
    static RainSystem s_instance;
    return s_instance;
}

RainSystem::RainSystem() {
    m_splashes.resize(8192);
}

RainSystem::~RainSystem() {
    Shutdown();
}

#if defined(_WIN32)
bool RainSystem::Initialize(ID3D12Device* device) {
    if (!device) return false;
    m_device = device;

    ReallocateParticles(GetParticleCountForQuality(m_settings.quality));
    if (!CreatePipelines()) return false;
    if (!CreateBuffers(100000, 8192)) return false;

    return true;
}
#else
bool RainSystem::Initialize(void* device) {
    ReallocateParticles(GetParticleCountForQuality(m_settings.quality));
    return true;
}
#endif

void RainSystem::Shutdown() {
    m_particles.clear();
    m_splashes.clear();
    m_streakVertices.clear();
    m_streakIndices.clear();
    m_splashVertices.clear();
    m_splashIndices.clear();

#if defined(_WIN32)
    if (m_constantBuffer && m_pCbvData) {
        m_constantBuffer->Unmap(0, nullptr);
        m_pCbvData = nullptr;
    }

    m_streakVertexBuffer.Reset();
    m_streakIndexBuffer.Reset();
    m_splashVertexBuffer.Reset();
    m_splashIndexBuffer.Reset();
    m_constantBuffer.Reset();
    m_streakPipelineState.Reset();
    m_splashPipelineState.Reset();
    m_rootSignature.Reset();
    m_device.Reset();
#endif
}

void RainSystem::ReallocateParticles(size_t count) {
    size_t prevCount = m_particles.size();
    m_particles.resize(count);

    for (size_t i = prevCount; i < count; ++i) {
        RainDrop& d = m_particles[i];
        d.randomSeed = static_cast<float>(rand()) / RAND_MAX;
        d.length = m_settings.dropLength * (0.8f + 0.4f * d.randomSeed);
        d.width = m_settings.dropWidth * (0.8f + 0.4f * d.randomSeed);
        d.alpha = 0.35f + 0.25f * d.randomSeed;

        float r = (static_cast<float>(rand()) / RAND_MAX) * m_settings.areaRadius;
        float theta = (static_cast<float>(rand()) / RAND_MAX) * XM_2PI;
        float y = (static_cast<float>(rand()) / RAND_MAX) * m_settings.areaHeight;

        d.position = XMFLOAT3(r * cosf(theta), y, r * sinf(theta));
        d.velocity = XMFLOAT3(0.0f, -m_settings.fallSpeed * (0.85f + 0.3f * d.randomSeed), 0.0f);
        d.alive = true;
    }
}

#if defined(_WIN32)
bool RainSystem::CreatePipelines() {
    // 1. Root Signature (1 CBV at b0)
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob))) {
        if (errorBlob) {
            std::cout << "Rain RootSignature Error: " << (char*)errorBlob->GetBufferPointer() << "\n";
        }
        return false;
    }

    if (FAILED(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)))) {
        return false;
    }

    // 2. Compile Shaders
    ComPtr<ID3DBlob> vsStreakBlob, psStreakBlob, vsSplashBlob, psSplashBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;

    D3DCompile(g_RainShaderCode, strlen(g_RainShaderCode), "RainShaders", nullptr, nullptr, "VSStreak", "vs_5_1", compileFlags, 0, &vsStreakBlob, nullptr);
    D3DCompile(g_RainShaderCode, strlen(g_RainShaderCode), "RainShaders", nullptr, nullptr, "PSStreak", "ps_5_1", compileFlags, 0, &psStreakBlob, nullptr);
    D3DCompile(g_RainShaderCode, strlen(g_RainShaderCode), "RainShaders", nullptr, nullptr, "VSSplash", "vs_5_1", compileFlags, 0, &vsSplashBlob, nullptr);
    D3DCompile(g_RainShaderCode, strlen(g_RainShaderCode), "RainShaders", nullptr, nullptr, "PSSplash", "ps_5_1", compileFlags, 0, &psSplashBlob, nullptr);

    if (!vsStreakBlob || !psStreakBlob || !vsSplashBlob || !psSplashBlob) {
        return false;
    }

    // 3. Input Layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Blend state: Standard alpha blending
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth state: Read-only depth test
    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Do not write depth for transparent rain
    depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthDesc.StencilEnable = FALSE;

    // Rasterizer
    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE; // Double sided
    rastDesc.DepthClipEnable = TRUE;

    // Streak PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsStreakBlob->GetBufferPointer(), vsStreakBlob->GetBufferSize() };
    psoDesc.PS = { psStreakBlob->GetBufferPointer(), psStreakBlob->GetBufferSize() };
    psoDesc.BlendState = blendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = rastDesc;
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(EngineRender::PipelineStateCache::GraphicsForDevice(m_device.Get(),"rain_m_streakPipelineState",&psoDesc,IID_PPV_ARGS(&m_streakPipelineState)))) {
        return false;
    }

    // Splash PSO
    psoDesc.VS = { vsSplashBlob->GetBufferPointer(), vsSplashBlob->GetBufferSize() };
    psoDesc.PS = { psSplashBlob->GetBufferPointer(), psSplashBlob->GetBufferSize() };
    if (FAILED(EngineRender::PipelineStateCache::GraphicsForDevice(m_device.Get(),"rain_m_splashPipelineState",&psoDesc,IID_PPV_ARGS(&m_splashPipelineState)))) {
        return false;
    }

    return true;
}

bool RainSystem::CreateBuffers(size_t maxStreaks, size_t maxSplashes) {
    m_maxStreaks = maxStreaks;
    m_maxSplashes = maxSplashes;

    D3D12_HEAP_PROPERTIES heapUpload = {};
    heapUpload.Type = D3D12_HEAP_TYPE_UPLOAD;

    // 1. Streak Buffers (4 vertices, 6 indices per streak)
    size_t streakVBSize = maxStreaks * 4 * sizeof(RainVertex);
    size_t streakIBSize = maxStreaks * 6 * sizeof(uint32_t);

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = streakVBSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_streakVertexBuffer)))) {
        return false;
    }

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = streakIBSize;
    if (FAILED(m_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_streakIndexBuffer)))) {
        return false;
    }

    m_streakVBView.BufferLocation = m_streakVertexBuffer->GetGPUVirtualAddress();
    m_streakVBView.StrideInBytes = sizeof(RainVertex);
    m_streakVBView.SizeInBytes = static_cast<UINT>(streakVBSize);

    m_streakIBView.BufferLocation = m_streakIndexBuffer->GetGPUVirtualAddress();
    m_streakIBView.Format = DXGI_FORMAT_R32_UINT;
    m_streakIBView.SizeInBytes = static_cast<UINT>(streakIBSize);

    // 2. Splash Buffers (4 vertices, 6 indices per splash)
    size_t splashVBSize = maxSplashes * 4 * sizeof(RainVertex);
    size_t splashIBSize = maxSplashes * 6 * sizeof(uint32_t);

    vbDesc.Width = splashVBSize;
    if (FAILED(m_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_splashVertexBuffer)))) {
        return false;
    }

    ibDesc.Width = splashIBSize;
    if (FAILED(m_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_splashIndexBuffer)))) {
        return false;
    }

    m_splashVBView.BufferLocation = m_splashVertexBuffer->GetGPUVirtualAddress();
    m_splashVBView.StrideInBytes = sizeof(RainVertex);
    m_splashVBView.SizeInBytes = static_cast<UINT>(splashVBSize);

    m_splashIBView.BufferLocation = m_splashIndexBuffer->GetGPUVirtualAddress();
    m_splashIBView.Format = DXGI_FORMAT_R32_UINT;
    m_splashIBView.SizeInBytes = static_cast<UINT>(splashIBSize);

    // 3. Constant Buffer
    D3D12_RESOURCE_DESC cbDesc = vbDesc;
    cbDesc.Width = (sizeof(RainCB) + 255) & ~255;
    if (FAILED(m_device->CreateCommittedResource(&heapUpload, D3D12_HEAP_FLAG_NONE, &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)))) {
        return false;
    }

    D3D12_RANGE readRange = { 0, 0 };
    m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvData));

    return true;
}
#else
bool RainSystem::CreatePipelines() { return true; }
bool RainSystem::CreateBuffers(size_t maxStreaks, size_t maxSplashes) {
    m_maxStreaks = maxStreaks;
    m_maxSplashes = maxSplashes;
    return true;
}
#endif

void RainSystem::SpawnSplash(const XMFLOAT3& pos, const XMFLOAT3& normal) {
    if (!m_settings.surface.enabled || !m_settings.surface.splashesEnabled || m_splashes.empty()) return;

    float intensity = (std::clamp)(m_settings.surface.splashIntensity,0.f,2.f);
    if(intensity<=0)return;
    // A contact-anchored capillary wave; never moves with the rain volume.
    {
        auto& ring=m_splashes[m_nextSplashIndex];m_nextSplashIndex=(m_nextSplashIndex+1)%m_splashes.size();
        ring=RainSplash{};ring.active=true;ring.type=0;ring.normal=normal;
        ring.position={pos.x+normal.x*.004f,pos.y+normal.y*.004f,pos.z+normal.z*.004f};
        ring.radius=.14f+float(rand())/RAND_MAX*.10f;ring.maxRadius=ring.radius;
        ring.maxLife=.4f+float(rand())/RAND_MAX*.2f;ring.alpha=intensity;
    }

    // 1. Vertical splash crown / impact burst (type 2)
    {
        auto& s = m_splashes[m_nextSplashIndex];
        m_nextSplashIndex = (m_nextSplashIndex + 1) % m_splashes.size();

        s.active = true;
        s.type = 2; // Vertical splash crown burst
        s.position = XMFLOAT3(pos.x, pos.y + 0.002f, pos.z);
        s.normal = normal;
        s.radius = 0.035f + (static_cast<float>(rand()) / RAND_MAX) * 0.020f; // Crown width
        s.maxRadius = s.radius * 1.25f;
        s.life = 0.0f;
        s.maxLife = 0.11f + (static_cast<float>(rand()) / RAND_MAX) * 0.06f; // Fast subtle impact pop (70-100ms)
        s.alpha = intensity * 0.75f;
    }

    // 3. Leaping vertical micro-droplet splash beads (type 1)
    int beadCount = 3; // 2 subtle micro-beads
    for (int b = 0; b < beadCount; ++b) {
        auto& s = m_splashes[m_nextSplashIndex];
        m_nextSplashIndex = (m_nextSplashIndex + 1) % m_splashes.size();

        s.active = true;
        s.type = 1; // Vertical droplet bead
        s.position = XMFLOAT3(pos.x, pos.y + 0.006f, pos.z);
        float angle = (static_cast<float>(rand()) / RAND_MAX) * XM_2PI;
        float spd = 0.4f + (static_cast<float>(rand()) / RAND_MAX) * 0.8f;
        s.velocity = XMFLOAT3(cosf(angle) * spd, 1.1f + (static_cast<float>(rand()) / RAND_MAX) * 1.0f, sinf(angle) * spd);
        s.radius = 0.006f + (static_cast<float>(rand()) / RAND_MAX) * 0.005f; // 4 to 9 mm
        s.maxRadius = s.radius;
        s.life = 0.0f;
        s.maxLife = 0.22f + (static_cast<float>(rand()) / RAND_MAX) * 0.10f;
        s.alpha = intensity * 0.70f;
    }
}

void RainSystem::Update(float dt, const XMFLOAT3& cameraPos, const std::vector<Camera::CollisionBox>& collisionBoxes) {
    auto startTime = std::chrono::high_resolution_clock::now();

    m_weather.Update(dt, m_settings);
    m_accumulatedTime += dt;

    m_collisionCounterTimer += dt;
    if (m_collisionCounterTimer >= 1.0f) {
        m_debugStats.collisionHitsPerSec = m_recentCollisions;
        m_recentCollisions = 0;
        m_collisionCounterTimer = 0.0f;
    }

    // 1. Realistic Wetness & Puddle Dynamic Simulation (Delayed water appearance & drying over time)
    bool isRaining = m_settings.enabled && (m_settings.intensity > 0.01f);

    if (isRaining && m_settings.surface.enabled) {
        // --- RAIN ACTIVE: ACCUMULATION PHASE ---

        // 1. Surface Wetness: dampness begins and accumulates smoothly first
        if (m_settings.surface.wetnessEnabled) {
            float targetWetness = (std::min)(1.0f, m_settings.intensity * 0.85f + 0.20f);
            if (m_settings.surface.wetness < targetWetness) {
                float accumSpeed = m_settings.surface.wetnessAccumRate * (m_settings.intensity * 0.7f + 0.3f);
                m_settings.surface.wetness = (std::min)(targetWetness, m_settings.surface.wetness + accumSpeed * dt);
            } else if (m_settings.surface.wetness > targetWetness) {
                m_settings.surface.wetness = (std::max)(targetWetness, m_settings.surface.wetness - m_settings.surface.wetnessDryRate * dt);
            }
        } else {
            m_settings.surface.wetness = (std::max)(0.0f, m_settings.surface.wetness - m_settings.surface.wetnessDryRate * dt);
        }

        // 2. Puddles: only begin to form and collect water after the ground is already wet (wetness > 0.35)
        if (m_settings.surface.puddlesEnabled) {
            if (m_settings.surface.wetness > 0.35f) {
                float wetSaturation = (m_settings.surface.wetness - 0.35f) / 0.65f; // 0.0 -> 1.0
                float targetPuddle = (std::min)(1.0f, (std::max)(0.0f, m_settings.intensity * 0.90f - 0.10f)) * wetSaturation;

                if (m_settings.surface.puddleAmount < targetPuddle) {
                    float puddleAccumSpeed = m_settings.surface.puddleAccumRate * wetSaturation * (m_settings.intensity * 0.6f + 0.4f);
                    m_settings.surface.puddleAmount = (std::min)(targetPuddle, m_settings.surface.puddleAmount + puddleAccumSpeed * dt);
                } else if (m_settings.surface.puddleAmount > targetPuddle) {
                    m_settings.surface.puddleAmount = (std::max)(targetPuddle, m_settings.surface.puddleAmount - m_settings.surface.puddleDryRate * dt);
                }
            } else {
                // Ground not wet enough yet for puddles to pool
                m_settings.surface.puddleAmount = (std::max)(0.0f, m_settings.surface.puddleAmount - m_settings.surface.puddleDryRate * dt);
            }
        } else {
            m_settings.surface.puddleAmount = (std::max)(0.0f, m_settings.surface.puddleAmount - m_settings.surface.puddleDryRate * dt);
        }
    } else {
        // --- RAIN STOPPED / CLEAR: DRYING & EVAPORATION PHASE ---

        // 1. Puddles dry out and drain away first
        m_settings.surface.puddleAmount = (std::max)(0.0f, m_settings.surface.puddleAmount - m_settings.surface.puddleDryRate * dt);

        // 2. Damp surfaces dry out as puddles recede (accelerates once standing water is gone)
        float dryRateMultiplier = (m_settings.surface.puddleAmount > 0.05f) ? 0.35f : 1.0f;
        m_settings.surface.wetness = (std::max)(0.0f, m_settings.surface.wetness - m_settings.surface.wetnessDryRate * dryRateMultiplier * dt);
    }

    m_debugStats.currentWetness = m_settings.surface.wetness * m_settings.surface.wetnessStrength;
    m_debugStats.currentPuddle = m_settings.surface.puddleAmount * m_settings.surface.puddleAccumulation;
    m_debugStats.isLightningActive = m_weather.IsLightningActive();

    if (!m_settings.enabled) {
        m_debugStats.activeParticles = 0;
        m_debugStats.activeSplashes = 0;
        m_streakVertices.clear();
        m_streakIndices.clear();
        m_splashVertices.clear();
        m_splashIndices.clear();
        for(auto& splash:m_splashes)splash.active=false;
        m_nextSplashIndex=0;
        m_settings.surface.wetness = 0.0f;
        m_settings.surface.puddleAmount = 0.0f;
        return;
    }

    if(m_splashes.empty()){m_splashes.resize(8192);m_nextSplashIndex=0;}

    // Target particle count
    size_t targetCount = m_settings.useCustomParticleCount 
        ? static_cast<size_t>(m_settings.particleCount) 
        : static_cast<size_t>(GetParticleCountForQuality(m_settings.quality) * m_settings.intensity);
    size_t maxAllowed = m_maxStreaks > 0 ? m_maxStreaks : 100000;
    targetCount = (std::max)(size_t(1000), (std::min)(maxAllowed, targetCount));

    if (m_particles.size() != targetCount) {
        ReallocateParticles(targetCount);
    }

    // 2. Compute Wind Vector with gust turbulence
    XMFLOAT3 windFinal = { 0.0f, 0.0f, 0.0f };
    if (m_settings.wind.enabled) {
        float gust = m_settings.wind.gustStrength * sinf(m_accumulatedTime * m_settings.wind.gustFrequency * XM_2PI);
        float totalWindSpeed = (std::max)(0.0f, m_settings.wind.speed + gust);

        XMVECTOR windDir = XMLoadFloat3(&m_settings.wind.direction);
        windDir = XMVector3Normalize(windDir);
        XMVECTOR windVec = windDir * totalWindSpeed;
        XMStoreFloat3(&windFinal, windVec);
    }
    m_debugStats.currentWind = windFinal;

    float areaR = m_settings.areaRadius;
    float areaH = m_settings.areaHeight;

    float minX = cameraPos.x - areaR;
    float maxX = cameraPos.x + areaR;
    float minZ = cameraPos.z - areaR;
    float maxZ = cameraPos.z + areaR;
    float minY = -0.5f; // Always allow rain to reach ground level (y=0) regardless of camera height
    float maxY = (std::max)(cameraPos.y + areaH, 18.0f);

    // 3. Update Particles & Collisions
    m_streakVertices.clear();
    m_streakIndices.clear();

    XMVECTOR camPosVec = XMLoadFloat3(&cameraPos);

    for (size_t i = 0; i < m_particles.size(); ++i) {
        RainDrop& drop = m_particles[i];

        // Velocity: Wind + Gravity + Drop seed variation
        drop.velocity.x = windFinal.x + (drop.randomSeed - 0.5f) * 1.5f;
        drop.velocity.z = windFinal.z + (drop.randomSeed - 0.5f) * 1.5f;
        drop.velocity.y = -m_settings.fallSpeed * (0.85f + 0.3f * drop.randomSeed);

        // Previous position for collision segment
        XMFLOAT3 prevPos = drop.position;

        drop.position.x += drop.velocity.x * dt;
        drop.position.y += drop.velocity.y * dt;
        drop.position.z += drop.velocity.z * dt;

        bool collided = false;
        bool sheltered = false;
        XMFLOAT3 hitPos = drop.position;
        XMFLOAT3 hitNormal = { 0.0f, 1.0f, 0.0f };

        // Collision & Shelter tests if surface interaction is enabled
        if (m_settings.surface.enabled) {
            // A. Check against all 3D scene boxes (buildings, cubes, models)
            if (!collisionBoxes.empty()) {
                for (const auto& box : collisionBoxes) {
                    if (drop.position.x >= (box.minPoint.x - 0.05f) && drop.position.x <= (box.maxPoint.x + 0.05f) &&
                        drop.position.z >= (box.minPoint.z - 0.05f) && drop.position.z <= (box.maxPoint.z + 0.05f)) {
                        
                        // 1. Direct hit on top surface of model/cube/roof
                        if (prevPos.y >= (box.maxPoint.y - 0.02f) && drop.position.y <= (box.maxPoint.y + 0.15f)) {
                            collided = true;
                            hitPos = XMFLOAT3(drop.position.x, box.maxPoint.y + 0.02f, drop.position.z);
                            hitNormal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                            break;
                        }

                        // 2. Drop is under the model (shelter / overhang) or inside the model volume
                        if (drop.position.y < box.maxPoint.y && drop.position.y >= (box.minPoint.y - 0.2f)) {
                            sheltered = true;
                            break;
                        }
                    }
                }
            }

            // B. Ground collision (y = 0) if not under shelter
            if (!collided && !sheltered) {
                if (prevPos.y >= -0.05f && drop.position.y <= 0.15f) {
                    collided = true;
                    hitPos = XMFLOAT3(drop.position.x, 0.02f, drop.position.z);
                    hitNormal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                }
            }
        }

        if (collided) {
            SpawnSplash(hitPos, hitNormal);
            m_recentCollisions++;

            // Respawn drop at top
            drop.position.y = maxY;
            drop.position.x = cameraPos.x + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
            drop.position.z = cameraPos.z + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
            continue;
        }

        if (sheltered) {
            // Rain is blocked by the 3D model above it: no drop can exist in this shadow column!
            drop.position.y = maxY;
            drop.position.x = cameraPos.x + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
            drop.position.z = cameraPos.z + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
            continue;
        }

        // Volume boundary wrapping
        if (drop.position.y < minY || drop.position.x < minX || drop.position.x > maxX || drop.position.z < minZ || drop.position.z > maxZ) {
            drop.position.y = maxY;
            drop.position.x = cameraPos.x + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
            drop.position.z = cameraPos.z + ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * areaR;
        }

        // 4. Build Billboard Streak Geometry
        XMVECTOR posVec = XMLoadFloat3(&drop.position);
        XMVECTOR velVec = XMLoadFloat3(&drop.velocity);
        XMVECTOR velLenSq = XMVector3LengthSq(velVec);
        XMVECTOR velNorm = (XMVectorGetX(velLenSq) < 1e-6f) ? XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f) : XMVector3Normalize(velVec);

        XMVECTOR diffCam = XMVectorSubtract(camPosVec, posVec);
        XMVECTOR diffLenSq = XMVector3LengthSq(diffCam);
        if (XMVectorGetX(diffLenSq) < 1e-4f) {
            continue; // Skip raindrops intersecting camera origin to prevent NaN
        }
        XMVECTOR toCam = XMVector3Normalize(diffCam);

        float currentDropWidth = m_settings.dropWidth * (0.8f + 0.4f * drop.randomSeed);
        float currentDropLength = m_settings.dropLength * (0.8f + 0.4f * drop.randomSeed);

        XMVECTOR crossVec = XMVector3Cross(velNorm, toCam);
        XMVECTOR crossLenSq = XMVector3LengthSq(crossVec);
        XMVECTOR side;
        if (XMVectorGetX(crossLenSq) < 1e-4f) {
            side = XMVectorSet(currentDropWidth * 0.5f, 0.0f, 0.0f, 0.0f);
        } else {
            side = XMVector3Normalize(crossVec) * (currentDropWidth * 0.5f);
        }

        XMVECTOR startP = posVec;
        XMVECTOR endP = posVec - velNorm * currentDropLength;

        XMFLOAT3 v0, v1, v2, v3;
        XMStoreFloat3(&v0, startP - side);
        XMStoreFloat3(&v1, startP + side);
        XMStoreFloat3(&v2, endP + side);
        XMStoreFloat3(&v3, endP - side);

        float volumeOpacity = RainVolumeOpacity(drop.position.x, drop.position.z, cameraPos.x, cameraPos.z, areaR);
        if (volumeOpacity <= 0.001f) continue;
        XMFLOAT4 streakCol = XMFLOAT4(1.0f, 1.0f, 1.0f, drop.alpha * volumeOpacity);

        if (m_streakVertices.size() + 4 > m_maxStreaks * 4 || m_streakIndices.size() + 6 > m_maxStreaks * 6) {
            break;
        }

        uint32_t baseIdx = static_cast<uint32_t>(m_streakVertices.size());

        m_streakVertices.push_back({ v0, XMFLOAT2(0.0f, 1.0f), streakCol });
        m_streakVertices.push_back({ v1, XMFLOAT2(1.0f, 1.0f), streakCol });
        m_streakVertices.push_back({ v2, XMFLOAT2(1.0f, 0.0f), streakCol });
        m_streakVertices.push_back({ v3, XMFLOAT2(0.0f, 0.0f), streakCol });

        m_streakIndices.push_back(baseIdx + 0);
        m_streakIndices.push_back(baseIdx + 1);
        m_streakIndices.push_back(baseIdx + 2);
        m_streakIndices.push_back(baseIdx + 0);
        m_streakIndices.push_back(baseIdx + 2);
        m_streakIndices.push_back(baseIdx + 3);
    }

    // 5. Update Splashes & Ripples
    m_splashVertices.clear();
    m_splashIndices.clear();
    int activeSplashes = 0;

    for (auto& s : m_splashes) {
        if (!s.active) continue;

        s.life += dt;
        if (s.life >= s.maxLife) {
            s.active = false;
            continue;
        }

        if (m_splashVertices.size() + 4 > m_maxSplashes * 4 || m_splashIndices.size() + 6 > m_maxSplashes * 6) {
            continue;
        }

        activeSplashes++;
        float lifeRatio = s.life / s.maxLife;
        float volumeOpacity = RainVolumeOpacity(s.position.x, s.position.z, cameraPos.x, cameraPos.z, areaR);
        float alpha = s.alpha * volumeOpacity * (1.0f - lifeRatio);

        if(!m_settings.surface.enabled||!m_settings.surface.splashesEnabled){s.active=false;continue;}
        if(s.type==0){
            auto n=XMVector3Normalize(XMLoadFloat3(&s.normal));
            auto reference=std::abs(s.normal.y)>.9f?XMVectorSet(1,0,0,0):XMVectorSet(0,1,0,0);
            auto right=XMVector3Normalize(XMVector3Cross(reference,n))*s.radius;
            auto tangent=XMVector3Normalize(XMVector3Cross(n,right))*s.radius;
            auto centre=XMLoadFloat3(&s.position);XMFLOAT3 p[4];
            XMStoreFloat3(&p[0],centre-right-tangent);XMStoreFloat3(&p[1],centre+right-tangent);
            XMStoreFloat3(&p[2],centre+right+tangent);XMStoreFloat3(&p[3],centre-right+tangent);
            uint32_t base=static_cast<uint32_t>(m_splashVertices.size());
            const XMFLOAT2 uv[4]={{0,0},{1,0},{1,1},{0,1}};
            for(int i=0;i<4;++i)m_splashVertices.push_back({p[i],uv[i],XMFLOAT4(0,lifeRatio,0,s.alpha*volumeOpacity)});
            for(auto i:{0u,1u,2u,0u,2u,3u})m_splashIndices.push_back(base+i);
        } else if (s.type == 1) {
            // Type 1: Vertical leaping splash droplet bead
            s.position.x += s.velocity.x * dt;
            s.position.y += s.velocity.y * dt;
            s.position.z += s.velocity.z * dt;
            s.velocity.y -= 9.8f * dt; // Gravity deceleration

            XMVECTOR beadPos = XMLoadFloat3(&s.position);
            XMVECTOR diff = XMVectorSubtract(camPosVec, beadPos);
            XMVECTOR diffLenSq = XMVector3LengthSq(diff);
            if (XMVectorGetX(diffLenSq) < 1e-4f) continue;
            XMVECTOR toCam = XMVector3Normalize(diff);
            XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMVECTOR crossVec = XMVector3Cross(upVec, toCam);
            XMVECTOR crossLenSq = XMVector3LengthSq(crossVec);
            XMVECTOR rightVec;
            if (XMVectorGetX(crossLenSq) < 1e-4f) {
                rightVec = XMVectorSet(s.radius, 0.0f, 0.0f, 0.0f);
            } else {
                rightVec = XMVector3Normalize(crossVec) * s.radius;
            }
            XMVECTOR upScaled = upVec * s.radius;

            XMFLOAT3 p0, p1, p2, p3;
            XMStoreFloat3(&p0, beadPos - rightVec - upScaled);
            XMStoreFloat3(&p1, beadPos + rightVec - upScaled);
            XMStoreFloat3(&p2, beadPos + rightVec + upScaled);
            XMStoreFloat3(&p3, beadPos - rightVec + upScaled);

            // color: r = type (1.0f), g = lifeRatio, b = 0.0f, a = s.alpha
            XMFLOAT4 splashCol = XMFLOAT4(1.0f, lifeRatio, 0.0f, s.alpha * volumeOpacity);
            uint32_t baseIdx = static_cast<uint32_t>(m_splashVertices.size());

            m_splashVertices.push_back({ p0, XMFLOAT2(0.0f, 0.0f), splashCol });
            m_splashVertices.push_back({ p1, XMFLOAT2(1.0f, 0.0f), splashCol });
            m_splashVertices.push_back({ p2, XMFLOAT2(1.0f, 1.0f), splashCol });
            m_splashVertices.push_back({ p3, XMFLOAT2(0.0f, 1.0f), splashCol });

            m_splashIndices.push_back(baseIdx + 0);
            m_splashIndices.push_back(baseIdx + 1);
            m_splashIndices.push_back(baseIdx + 2);
            m_splashIndices.push_back(baseIdx + 0);
            m_splashIndices.push_back(baseIdx + 2);
            m_splashIndices.push_back(baseIdx + 3);
        } else {
            // Type 2: Vertical Splash Crown / Impact Burst (Standing billboard facing camera)
            float currentRadius = s.radius + (s.maxRadius - s.radius) * lifeRatio;
            float crownHeight = currentRadius * 1.15f;

            XMVECTOR basePos = XMLoadFloat3(&s.position);
            XMVECTOR toCam = XMVectorSubtract(camPosVec, basePos);
            // Project toCam onto horizontal plane to keep crown vertical
            XMVECTOR horizToCam = XMVectorSet(XMVectorGetX(toCam), 0.0f, XMVectorGetZ(toCam), 0.0f);
            if (XMVectorGetX(XMVector3LengthSq(horizToCam)) < 0.001f) {
                horizToCam = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            } else {
                horizToCam = XMVector3Normalize(horizToCam);
            }

            XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(upVec, horizToCam)) * currentRadius;
            XMVECTOR upScaled = upVec * crownHeight;

            XMFLOAT3 p0, p1, p2, p3;
            // Base at y = s.position.y (uv.y = 0.0)
            // Top at y = s.position.y + crownHeight (uv.y = 1.0)
            XMStoreFloat3(&p0, basePos - rightVec);
            XMStoreFloat3(&p1, basePos + rightVec);
            XMStoreFloat3(&p2, basePos + rightVec + upScaled);
            XMStoreFloat3(&p3, basePos - rightVec + upScaled);

            // color: r = type (2.0f), g = lifeRatio, b = 0.0f, a = s.alpha
            XMFLOAT4 splashCol = XMFLOAT4(2.0f, lifeRatio, 0.0f, s.alpha * volumeOpacity);
            uint32_t baseIdx = static_cast<uint32_t>(m_splashVertices.size());

            m_splashVertices.push_back({ p0, XMFLOAT2(0.0f, 0.0f), splashCol });
            m_splashVertices.push_back({ p1, XMFLOAT2(1.0f, 0.0f), splashCol });
            m_splashVertices.push_back({ p2, XMFLOAT2(1.0f, 1.0f), splashCol });
            m_splashVertices.push_back({ p3, XMFLOAT2(0.0f, 1.0f), splashCol });

            m_splashIndices.push_back(baseIdx + 0);
            m_splashIndices.push_back(baseIdx + 1);
            m_splashIndices.push_back(baseIdx + 2);
            m_splashIndices.push_back(baseIdx + 0);
            m_splashIndices.push_back(baseIdx + 2);
            m_splashIndices.push_back(baseIdx + 3);
        }
    }

#if defined(_WIN32)
    // 6. Upload Streak & Splash data to GPU upload buffers
    if (!m_streakVertices.empty() && m_streakVertexBuffer && m_streakIndexBuffer) {
        void* pData = nullptr;
        D3D12_RANGE readRange = { 0, 0 };

        size_t maxVbSize = m_maxStreaks * 4 * sizeof(RainVertex);
        size_t vbSize = (std::min)(m_streakVertices.size() * sizeof(RainVertex), maxVbSize);
        if (SUCCEEDED(m_streakVertexBuffer->Map(0, &readRange, &pData)) && pData) {
            std::memcpy(pData, m_streakVertices.data(), vbSize);
            m_streakVertexBuffer->Unmap(0, nullptr);
        }

        size_t maxIbSize = m_maxStreaks * 6 * sizeof(uint32_t);
        size_t ibSize = (std::min)(m_streakIndices.size() * sizeof(uint32_t), maxIbSize);
        if (SUCCEEDED(m_streakIndexBuffer->Map(0, &readRange, &pData)) && pData) {
            std::memcpy(pData, m_streakIndices.data(), ibSize);
            m_streakIndexBuffer->Unmap(0, nullptr);
        }
    }

    if (!m_splashVertices.empty() && m_splashVertexBuffer && m_splashIndexBuffer) {
        void* pData = nullptr;
        D3D12_RANGE readRange = { 0, 0 };

        size_t maxVbSize = m_maxSplashes * 4 * sizeof(RainVertex);
        size_t vbSize = (std::min)(m_splashVertices.size() * sizeof(RainVertex), maxVbSize);
        if (SUCCEEDED(m_splashVertexBuffer->Map(0, &readRange, &pData)) && pData) {
            std::memcpy(pData, m_splashVertices.data(), vbSize);
            m_splashVertexBuffer->Unmap(0, nullptr);
        }

        size_t maxIbSize = m_maxSplashes * 6 * sizeof(uint32_t);
        size_t ibSize = (std::min)(m_splashIndices.size() * sizeof(uint32_t), maxIbSize);
        if (SUCCEEDED(m_splashIndexBuffer->Map(0, &readRange, &pData)) && pData) {
            std::memcpy(pData, m_splashIndices.data(), ibSize);
            m_splashIndexBuffer->Unmap(0, nullptr);
        }
    }
#endif

    auto endTime = std::chrono::high_resolution_clock::now();
    m_debugStats.simulationTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    m_debugStats.activeParticles = static_cast<int>(m_particles.size());
    m_debugStats.activeSplashes = activeSplashes;
}

#if defined(_WIN32)
void RainSystem::Render(ID3D12GraphicsCommandList* commandList, const XMMATRIX& viewProj, const XMFLOAT3& lightColor, float lightningIntensity, const XMFLOAT3& cameraPos, float time, const XMFLOAT3& lightDir) {
    if (!m_settings.enabled || !commandList || !m_streakPipelineState || !m_rootSignature) return;

    auto startTime = std::chrono::high_resolution_clock::now();

    // 1. Update Constant Buffer
    if (m_pCbvData) {
        m_pCbvData->viewProj = XMMatrixTranspose(viewProj);
        m_pCbvData->lightColorAndIntensity = XMFLOAT4(lightColor.x, lightColor.y, lightColor.z, lightningIntensity);
        m_pCbvData->cameraPosAndTime = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, time);
        m_pCbvData->lightDir = XMFLOAT4(lightDir.x, lightDir.y, lightDir.z, 0.0f);
    }

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 2. Draw Rain Streaks
    if (!m_streakIndices.empty() && m_streakVertexBuffer && m_streakIndexBuffer) {
        UINT streakDrawCount = static_cast<UINT>((std::min)(m_streakIndices.size(), m_maxStreaks * 6));
        if (streakDrawCount > 0) {
            commandList->SetPipelineState(m_streakPipelineState.Get());
            commandList->IASetVertexBuffers(0, 1, &m_streakVBView);
            commandList->IASetIndexBuffer(&m_streakIBView);
            commandList->DrawIndexedInstanced(streakDrawCount, 1, 0, 0, 0);
        }
    }

    // 3. Draw Splashes & Ripples
    if (!m_splashIndices.empty() && m_splashVertexBuffer && m_splashIndexBuffer) {
        UINT splashDrawCount = static_cast<UINT>((std::min)(m_splashIndices.size(), m_maxSplashes * 6));
        if (splashDrawCount > 0) {
            commandList->SetPipelineState(m_splashPipelineState.Get());
            commandList->IASetVertexBuffers(0, 1, &m_splashVBView);
            commandList->IASetIndexBuffer(&m_splashIBView);
            commandList->DrawIndexedInstanced(splashDrawCount, 1, 0, 0, 0);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_debugStats.renderTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
}
#else
void RainSystem::Render(void* commandList, const XMMATRIX& viewProj, const XMFLOAT3& lightColor, float lightningIntensity, const XMFLOAT3& cameraPos, float time, const XMFLOAT3& lightDir) {
}
#endif

} // namespace EngineRain
