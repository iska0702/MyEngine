#include "ParticleSystem.h"
#if defined(_WIN32)
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineVFX {

static const char* g_ParticleShaderHLSL = R"(
cbuffer ParticleCB : register(b0) {
    float4x4 g_ViewProj;
    float4   g_CameraPosAndTime;
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
};

PSInput VSParticle(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), g_ViewProj);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSParticle(PSInput input) : SV_TARGET {
    float2 center = input.uv - float2(0.5f, 0.5f);
    float distSq = dot(center, center) * 4.0f;
    clip(1.0f - distSq);
    float falloff = saturate(1.0f - distSq);
    falloff = pow(falloff, 1.6f);
    return float4(input.color.rgb, input.color.a * falloff);
}
)";

ParticleSystem& ParticleSystem::Get() {
    static ParticleSystem instance;
    return instance;
}

ParticleSystem::ParticleSystem() = default;
ParticleSystem::~ParticleSystem() {
    Shutdown();
}

bool ParticleSystem::Initialize(ID3D12Device* device) {
    if (m_initialized) return true;
    m_device = device;
    if (!m_device) return false;

    if (!CreatePipelines()) {
        std::cout << "[ParticleSystem] Failed to create pipelines!\n";
        return false;
    }
    if (!CreateBuffers()) {
        std::cout << "[ParticleSystem] Failed to create dynamic buffers!\n";
        return false;
    }

    m_initialized = true;
    return true;
}

void ParticleSystem::Shutdown() {
    ClearEmitters();
    if (m_vertexBuffer && m_mappedVb) {
        m_vertexBuffer->Unmap(0, nullptr);
        m_mappedVb = nullptr;
    }
    if (m_indexBuffer && m_mappedIb) {
        m_indexBuffer->Unmap(0, nullptr);
        m_mappedIb = nullptr;
    }
    if (m_constantBuffer && m_mappedCbv) {
        m_constantBuffer->Unmap(0, nullptr);
        m_mappedCbv = nullptr;
    }
    m_constantBuffer.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_additivePSO.Reset();
    m_alphaPSO.Reset();
    m_rootSignature.Reset();
    m_device.Reset();
    m_initialized = false;
}

bool ParticleSystem::CreatePipelines() {
    // 1. Root Signature
    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParam.Descriptor.ShaderRegister = 0;
    rootParam.Descriptor.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob))) {
        if (errBlob) std::cout << "[ParticleSystem] Sig Error: " << (char*)errBlob->GetBufferPointer() << "\n";
        return false;
    }
    if (FAILED(m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)))) {
        return false;
    }

    // 2. Compile Shaders
    ComPtr<ID3DBlob> vsBlob, psBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    D3DCompile(g_ParticleShaderHLSL, strlen(g_ParticleShaderHLSL), "ParticleShaders", nullptr, nullptr, "VSParticle", "vs_5_1", compileFlags, 0, &vsBlob, nullptr);
    D3DCompile(g_ParticleShaderHLSL, strlen(g_ParticleShaderHLSL), "ParticleShaders", nullptr, nullptr, "PSParticle", "ps_5_1", compileFlags, 0, &psBlob, nullptr);

    if (!vsBlob || !psBlob) return false;

    // 3. Input Layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // 4. Base PSO Desc
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Transparent write-zero
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    // A. Additive PSO (fire, sparks, energy)
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_additivePSO)))) {
        return false;
    }

    // B. Alpha Blend PSO (smoke, dust)
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_alphaPSO)))) {
        return false;
    }

    return true;
}

bool ParticleSystem::CreateBuffers() {
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    // 1. Constant Buffer (256-byte aligned)
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = 256;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_constantBuffer)))) {
        return false;
    }
    m_constantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedCbv));

    // 2. Vertex Buffer
    D3D12_RESOURCE_DESC vbDesc = cbDesc;
    vbDesc.Width = sizeof(ParticleVertex) * MAX_RENDER_VERTICES * 2;
    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_vertexBuffer)))) {
        return false;
    }
    m_vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedVb));
    m_vbView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vbView.StrideInBytes = sizeof(ParticleVertex);
    m_vbView.SizeInBytes = static_cast<UINT>(vbDesc.Width);

    // 3. Index Buffer
    D3D12_RESOURCE_DESC ibDesc = cbDesc;
    ibDesc.Width = sizeof(uint32_t) * MAX_RENDER_INDICES * 2;
    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&m_indexBuffer)))) {
        return false;
    }
    m_indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedIb));
    m_ibView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_ibView.Format = DXGI_FORMAT_R32_UINT;
    m_ibView.SizeInBytes = static_cast<UINT>(ibDesc.Width);

    return true;
}

void ParticleSystem::Update(float dt, const XMFLOAT3& cameraPos, const XMFLOAT3& cameraUp) {
    if (!m_initialized) return;

    for (auto& emitter : m_emitters) {
        if (emitter) {
            emitter->Update(dt, cameraPos);
        }
    }

    // Prune finished one-shot emitters
    auto it = std::remove_if(m_emitters.begin(), m_emitters.end(),
                             [](const std::shared_ptr<ParticleEmitter>& e) {
                                 return e && e->IsFinished();
                             });
    m_emitters.erase(it, m_emitters.end());
}

void ParticleSystem::Render(ID3D12GraphicsCommandList* commandList,
                            const XMMATRIX& viewProj,
                            const XMFLOAT3& cameraPos,
                            float time) {
    if (!m_initialized || !commandList || m_emitters.empty() || !m_mappedCbv || !m_mappedVb || !m_mappedIb) {
        return;
    }

    // Update Constant Buffer
    m_mappedCbv->viewProj = XMMatrixTranspose(viewProj);
    m_mappedCbv->cameraPosAndTime = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, time);

    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_vbView);
    commandList->IASetIndexBuffer(&m_ibView);

    XMFLOAT3 camUp(0.0f, 1.0f, 0.0f);

    // Pass 1: Alpha Blend Emitters (Smoke, Dust)
    {
        std::vector<ParticleVertex> alphaVerts;
        std::vector<uint32_t> alphaIndices;
        for (const auto& emitter : m_emitters) {
            if (emitter && emitter->IsActive() && emitter->GetBlendMode() == BlendMode::Alpha) {
                emitter->GenerateVertices(cameraPos, camUp, alphaVerts, alphaIndices);
            }
        }

        if (!alphaIndices.empty() && alphaVerts.size() <= MAX_RENDER_VERTICES && alphaIndices.size() <= MAX_RENDER_INDICES) {
            memcpy(m_mappedVb, alphaVerts.data(), alphaVerts.size() * sizeof(ParticleVertex));
            memcpy(m_mappedIb, alphaIndices.data(), alphaIndices.size() * sizeof(uint32_t));

            commandList->SetPipelineState(m_alphaPSO.Get());
            commandList->DrawIndexedInstanced(static_cast<UINT>(alphaIndices.size()), 1, 0, 0, 0);
        }
    }

    // Pass 2: Additive Emitters (Fire, Sparks, Explosions, Magic)
    {
        std::vector<ParticleVertex> addVerts;
        std::vector<uint32_t> addIndices;
        for (const auto& emitter : m_emitters) {
            if (emitter && emitter->IsActive() && emitter->GetBlendMode() == BlendMode::Additive) {
                emitter->GenerateVertices(cameraPos, camUp, addVerts, addIndices);
            }
        }

        if (!addIndices.empty() && addVerts.size() <= MAX_RENDER_VERTICES && addIndices.size() <= MAX_RENDER_INDICES) {
            memcpy(m_mappedVb + MAX_RENDER_VERTICES, addVerts.data(), addVerts.size() * sizeof(ParticleVertex));
            memcpy(m_mappedIb + MAX_RENDER_INDICES, addIndices.data(), addIndices.size() * sizeof(uint32_t));

            commandList->SetPipelineState(m_additivePSO.Get());
            commandList->DrawIndexedInstanced(static_cast<UINT>(addIndices.size()), 1, static_cast<UINT>(MAX_RENDER_INDICES), static_cast<INT>(MAX_RENDER_VERTICES), 0);
        }
    }
}

std::shared_ptr<ParticleEmitter> ParticleSystem::SpawnEmitter(const ParticleEmitterConfig& config) {
    auto emitter = std::make_shared<ParticleEmitter>(config);
    m_emitters.push_back(emitter);
    return emitter;
}

void ParticleSystem::SpawnExplosion(const XMFLOAT3& position, float scale) {
    // 1. Fiery Core Fireball
    ParticleEmitterConfig core;
    core.name = "Explosion Core";
    core.type = EmitterType::Sphere;
    core.blendMode = BlendMode::Additive;
    core.position = position;
    core.boxExtents = XMFLOAT3(0.5f * scale, 0.5f * scale, 0.5f * scale);
    core.spawnRate = 0.0f;
    core.maxParticles = 80;
    core.minLifetime = 0.35f;
    core.maxLifetime = 0.75f;
    core.minSpeed = 4.0f * scale;
    core.maxSpeed = 9.0f * scale;
    core.coneAngle = 180.0f;
    core.startSize = 0.4f * scale;
    core.endSize   = 1.8f * scale;
    core.startColor = XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f);
    core.endColor   = XMFLOAT4(1.0f, 0.15f, 0.0f, 0.0f);
    core.gravity    = XMFLOAT3(0.0f, 1.5f, 0.0f);
    core.drag       = 1.5f;
    core.looping    = false;
    core.duration   = 0.8f;

    auto coreEmitter = SpawnEmitter(core);
    if (coreEmitter) coreEmitter->Burst(60);

    // 2. High-Velocity Sparks
    ParticleEmitterConfig sparks;
    sparks.name = "Explosion Sparks";
    sparks.type = EmitterType::Point;
    sparks.blendMode = BlendMode::Additive;
    sparks.position = position;
    sparks.spawnRate = 0.0f;
    sparks.maxParticles = 120;
    sparks.minLifetime = 0.6f;
    sparks.maxLifetime = 1.3f;
    sparks.minSpeed = 7.0f * scale;
    sparks.maxSpeed = 16.0f * scale;
    sparks.coneAngle = 180.0f;
    sparks.startSize = 0.12f;
    sparks.endSize   = 0.02f;
    sparks.startColor = XMFLOAT4(1.0f, 1.0f, 0.7f, 1.0f);
    sparks.endColor   = XMFLOAT4(1.0f, 0.4f, 0.0f, 0.0f);
    sparks.gravity    = XMFLOAT3(0.0f, -9.8f, 0.0f);
    sparks.drag       = 0.3f;
    sparks.looping    = false;
    sparks.duration   = 1.4f;

    auto sparkEmitter = SpawnEmitter(sparks);
    if (sparkEmitter) sparkEmitter->Burst(90);

    // 3. Lingering Smoke Plume
    ParticleEmitterConfig smoke;
    smoke.name = "Explosion Smoke";
    smoke.type = EmitterType::Sphere;
    smoke.blendMode = BlendMode::Alpha;
    smoke.position = position;
    smoke.boxExtents = XMFLOAT3(0.4f * scale, 0.4f * scale, 0.4f * scale);
    smoke.spawnRate = 0.0f;
    smoke.maxParticles = 40;
    smoke.minLifetime = 1.5f;
    smoke.maxLifetime = 2.8f;
    smoke.minSpeed = 1.2f * scale;
    smoke.maxSpeed = 3.2f * scale;
    smoke.coneAngle = 90.0f;
    smoke.direction = XMFLOAT3(0.0f, 1.0f, 0.0f);
    smoke.startSize = 0.5f * scale;
    smoke.endSize   = 2.2f * scale;
    smoke.startColor = XMFLOAT4(0.25f, 0.23f, 0.2f, 0.8f);
    smoke.endColor   = XMFLOAT4(0.1f, 0.1f, 0.1f, 0.0f);
    smoke.gravity    = XMFLOAT3(0.0f, 0.8f, 0.0f);
    smoke.drag       = 0.8f;
    smoke.looping    = false;
    smoke.duration   = 3.0f;

    auto smokeEmitter = SpawnEmitter(smoke);
    if (smokeEmitter) smokeEmitter->Burst(30);
}

void ParticleSystem::SpawnFire(const XMFLOAT3& position, float intensity) {
    ParticleEmitterConfig fire;
    fire.name = "Campfire Flame";
    fire.type = EmitterType::Box;
    fire.blendMode = BlendMode::Additive;
    fire.position = position;
    fire.direction = XMFLOAT3(0.0f, 1.0f, 0.0f);
    fire.boxExtents = XMFLOAT3(0.35f * intensity, 0.1f, 0.35f * intensity);
    fire.spawnRate = 50.0f * intensity;
    fire.maxParticles = 250;
    fire.minLifetime = 0.45f;
    fire.maxLifetime = 0.95f;
    fire.minSpeed = 1.2f;
    fire.maxSpeed = 3.0f * intensity;
    fire.coneAngle = 22.0f;
    fire.startSize = 0.25f * intensity;
    fire.endSize   = 0.65f * intensity;
    fire.startColor = XMFLOAT4(1.0f, 0.75f, 0.15f, 1.0f);
    fire.endColor   = XMFLOAT4(0.9f, 0.15f, 0.0f, 0.0f);
    fire.gravity    = XMFLOAT3(0.0f, 1.8f, 0.0f);
    fire.drag       = 0.5f;
    fire.looping    = true;

    SpawnEmitter(fire);
}

void ParticleSystem::SpawnSmoke(const XMFLOAT3& position, float duration) {
    ParticleEmitterConfig smoke;
    smoke.name = "Smoke Column";
    smoke.type = EmitterType::Box;
    smoke.blendMode = BlendMode::Alpha;
    smoke.position = position;
    smoke.direction = XMFLOAT3(0.0f, 1.0f, 0.0f);
    smoke.boxExtents = XMFLOAT3(0.4f, 0.1f, 0.4f);
    smoke.spawnRate = 22.0f;
    smoke.maxParticles = 200;
    smoke.minLifetime = 1.8f;
    smoke.maxLifetime = 3.2f;
    smoke.minSpeed = 1.0f;
    smoke.maxSpeed = 2.4f;
    smoke.coneAngle = 28.0f;
    smoke.startSize = 0.35f;
    smoke.endSize   = 2.0f;
    smoke.startColor = XMFLOAT4(0.3f, 0.3f, 0.32f, 0.7f);
    smoke.endColor   = XMFLOAT4(0.15f, 0.15f, 0.15f, 0.0f);
    smoke.gravity    = XMFLOAT3(0.0f, 0.6f, 0.0f);
    smoke.drag       = 0.6f;
    smoke.looping    = (duration <= 0.0f);
    smoke.duration   = duration;

    SpawnEmitter(smoke);
}

void ParticleSystem::SpawnWaterJet(const XMFLOAT3& position, const XMFLOAT3& direction, float speed, float spread) {
    ParticleEmitterConfig water;
    water.name = "Water Jet";
    water.type = EmitterType::Point;
    water.blendMode = BlendMode::Alpha;
    water.position = position;
    water.direction = direction;
    water.spawnRate = 260.0f;
    water.maxParticles = 800;
    water.minLifetime = 1.0f;
    water.maxLifetime = 1.8f;
    water.minSpeed = (std::max)(0.1f, speed * 0.88f);
    water.maxSpeed = (std::max)(water.minSpeed, speed);
    water.coneAngle = (std::clamp)(spread, 0.0f, 45.0f);
    water.startSize = 0.13f;
    water.endSize = 0.08f;
    water.startColor = XMFLOAT4(0.48f, 0.75f, 0.9f, 0.8f);
    water.endColor = XMFLOAT4(0.68f, 0.85f, 0.94f, 0.1f);
    water.gravity = XMFLOAT3(0.0f, -9.81f, 0.0f);
    water.drag = 0.05f;
    water.looping = true;
    SpawnEmitter(water);
}

void ParticleSystem::SpawnSparks(const XMFLOAT3& position, const XMFLOAT3& direction, size_t count) {
    ParticleEmitterConfig sparks;
    sparks.name = "Directional Sparks";
    sparks.type = EmitterType::Point;
    sparks.blendMode = BlendMode::Additive;
    sparks.position = position;
    sparks.direction = direction;
    sparks.spawnRate = 0.0f;
    sparks.maxParticles = count + 20;
    sparks.minLifetime = 0.4f;
    sparks.maxLifetime = 1.0f;
    sparks.minSpeed = 5.0f;
    sparks.maxSpeed = 14.0f;
    sparks.coneAngle = 45.0f;
    sparks.startSize = 0.1f;
    sparks.endSize   = 0.01f;
    sparks.startColor = XMFLOAT4(1.0f, 1.0f, 0.6f, 1.0f);
    sparks.endColor   = XMFLOAT4(1.0f, 0.3f, 0.0f, 0.0f);
    sparks.gravity    = XMFLOAT3(0.0f, -8.0f, 0.0f);
    sparks.drag       = 0.35f;
    sparks.looping    = false;
    sparks.duration   = 1.2f;

    auto emitter = SpawnEmitter(sparks);
    if (emitter) emitter->Burst(count);
}

void ParticleSystem::SpawnMagicEnergy(const XMFLOAT3& position, const XMFLOAT4& color) {
    ParticleEmitterConfig magic;
    magic.name = "Arcane Vortex";
    magic.type = EmitterType::Sphere;
    magic.blendMode = BlendMode::Additive;
    magic.position = position;
    magic.direction = XMFLOAT3(0.0f, 1.0f, 0.0f);
    magic.boxExtents = XMFLOAT3(1.2f, 0.4f, 1.2f);
    magic.spawnRate = 35.0f;
    magic.maxParticles = 220;
    magic.minLifetime = 0.8f;
    magic.maxLifetime = 1.8f;
    magic.minSpeed = 0.8f;
    magic.maxSpeed = 2.5f;
    magic.coneAngle = 70.0f;
    magic.startSize = 0.2f;
    magic.endSize   = 0.7f;
    magic.startColor = color;
    magic.endColor   = XMFLOAT4(color.x * 0.3f, color.y * 0.3f, color.z * 0.3f, 0.0f);
    magic.gravity    = XMFLOAT3(0.0f, 1.2f, 0.0f);
    magic.minRotSpeed = -3.0f;
    magic.maxRotSpeed =  3.0f;
    magic.looping    = true;

    SpawnEmitter(magic);
}

void ParticleSystem::ClearEmitters() {
    m_emitters.clear();
}

size_t ParticleSystem::GetTotalActiveParticles() const {
    size_t total = 0;
    for (const auto& e : m_emitters) {
        if (e) total += e->GetAliveCount();
    }
    return total;
}

void ParticleSystem::DrawUI(const XMFLOAT3& cameraPos, const XMFLOAT3& cameraForward) {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Particle / VFX Studio", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "VFX Statistics:");
    ImGui::Text("Active Emitters: %zu | Total Particles: %zu", m_emitters.size(), GetTotalActiveParticles());
    if (!m_initialized) ImGui::TextColored(ImVec4(1.f, 0.3f, 0.2f, 1.f), "Particle renderer unavailable; see the editor log.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Spawn Presets (At Cam Target):");

    XMFLOAT3 spawnTarget;
    spawnTarget.x = cameraPos.x + cameraForward.x * 4.0f;
    spawnTarget.y = (std::max)(0.0f, cameraPos.y + cameraForward.y * 4.0f);
    spawnTarget.z = cameraPos.z + cameraForward.z * 4.0f;

    if (ImGui::Button(" Explosion")) {
        SpawnExplosion(spawnTarget, 1.0f);
    }
    ImGui::SameLine();
    static float fireIntensity = 1.0f;
    if (ImGui::Button(" Fire")) {
        SpawnFire(spawnTarget, fireIntensity);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Sparks")) {
        SpawnSparks(spawnTarget, XMFLOAT3(0.0f, 1.0f, 0.0f), 50);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Smoke")) {
        SpawnSmoke(spawnTarget, 8.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button(" Magic Vortex")) {
        SpawnMagicEnergy(spawnTarget, XMFLOAT4(0.3f, 0.8f, 1.0f, 1.0f));
    }
    ImGui::SliderFloat("Fire intensity", &fireIntensity, 0.2f, 3.0f);

    static float waterSpeed = 7.0f;
    static float waterSpread = 3.0f;
    ImGui::SliderFloat("Water speed", &waterSpeed, 1.0f, 20.0f, "%.1f m/s");
    ImGui::SliderFloat("Water spread", &waterSpread, 0.0f, 30.0f, "%.1f deg");
    if (ImGui::Button(" Water jet")) {
        // Across the camera rather than directly away from it, so the jet is visible on creation.
        XMFLOAT3 direction(cameraForward.z, 0.7f, -cameraForward.x);
        SpawnWaterJet(spawnTarget, direction, waterSpeed, waterSpread);
    }

    ImGui::Separator();
    if (ImGui::Button(" Clear All Emitters")) {
        ClearEmitters();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "Active Emitters (%zu):", m_emitters.size());

    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(m_emitters.size()); ++i) {
        auto& emitter = m_emitters[i];
        if (!emitter) continue;

        ImGui::PushID(i + 6000);
        auto& cfg = emitter->GetConfig();
        char header[128];
        snprintf(header, sizeof(header), "[%d] %s (%zu alive)##%d",
                 i, cfg.name.c_str(), emitter->GetAliveCount(), i);

        if (ImGui::TreeNode(header)) {
            bool active = emitter->IsActive();
            if (ImGui::Checkbox("Active", &active)) emitter->SetActive(active);
            ImGui::SameLine();
            ImGui::Checkbox("Looping", &cfg.looping);
            ImGui::SameLine();
            if (ImGui::Button(" Burst 30")) emitter->Burst(30);
            ImGui::SameLine();
            if (ImGui::Button(" Remove")) removeIdx = i;

            ImGui::DragFloat3("Position", &cfg.position.x, 0.1f);
            ImGui::DragFloat3("Direction", &cfg.direction.x, 0.05f);
            ImGui::DragFloatRange2("Speed range", &cfg.minSpeed, &cfg.maxSpeed, 0.1f, 0.0f, 50.0f);
            ImGui::SliderFloat("Spread angle", &cfg.coneAngle, 0.0f, 90.0f, "%.1f deg");
            ImGui::SliderFloat("Spawn Rate", &cfg.spawnRate, 0.0f, 200.0f, "%.1f /s");
            ImGui::SliderFloat("Start Size", &cfg.startSize, 0.02f, 2.0f);
            ImGui::SliderFloat("End Size", &cfg.endSize, 0.02f, 4.0f);

            ImGui::ColorEdit4("Start Color", &cfg.startColor.x);
            ImGui::ColorEdit4("End Color", &cfg.endColor.x);
            ImGui::DragFloat3("Gravity", &cfg.gravity.x, 0.2f);
            ImGui::SliderFloat("Drag", &cfg.drag, 0.0f, 5.0f);

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (removeIdx >= 0 && removeIdx < static_cast<int>(m_emitters.size())) {
        m_emitters.erase(m_emitters.begin() + removeIdx);
    }

    ImGui::End();
#endif
}

} // namespace EngineVFX
#endif
