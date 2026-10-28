#pragma once

#include "../Common/IGraphicsBackend.h"
#include "MetalDevice.h"
#include "MetalBuffer.h"
#include "MetalTexture.h"
#include "MetalPipeline.h"
#include "../../Upscaling/MetalFX/MetalFXSystem.h"
#include "../../RayTracing/Metal/MetalRayTracing.h"
#include <vector>
#include <unordered_map>
#include <memory>

#ifdef __OBJC__
#import <QuartzCore/CAMetalLayer.h>
#import <dispatch/dispatch.h>
#endif

namespace EngineRender {

struct DynamicModelEntry {
    uint32_t id;
    std::string name;
    MetalBuffer vertexBuffer;
    MetalBuffer indexBuffer;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    PBRMaterial material;
    XMFLOAT3 position = { 0, 0, 0 };
    XMFLOAT3 rotation = { 0, 0, 0 };
    XMFLOAT3 scale = { 1, 1, 1 };
    XMFLOAT4X4 worldMatrix;
    XMFLOAT4X4 prevWorldMatrix;
    bool visible = true;
};

class MetalRendererBackend : public IGraphicsBackend {
public:
    MetalRendererBackend();
    ~MetalRendererBackend() override;

    bool Initialize(void* nativeWindowHandle, int width, int height, Camera* camera) override;
    void Destroy() override;

    void Resize(int width, int height) override;
    void BeginUI() override;
    void Render(float dt) override;
    bool IsDeviceRemoved() const override { return false; }

    const GraphicsCapabilities& GetCapabilities() const override;

    uint32_t CreateDynamicModel(const std::string& name,
                                const std::vector<Vertex>& vertices,
                                const std::vector<uint32_t>& indices,
                                const PBRMaterial& material) override;
    bool UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Vertex>& vertices) override;
    bool UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) override;
    bool RemoveDynamicModel(uint32_t modelId) override;

    void SetUpscaleTechnique(UpscaleTechnique technique) override;
    UpscaleTechnique GetUpscaleTechnique() const override { return m_upscaleTechnique; }
    void SetUpscaleQuality(UpscaleQuality quality) override;
    UpscaleQuality GetUpscaleQuality() const override { return m_upscaleQuality; }
    void SetUpscaleSharpness(float sharpness) override;
    float GetUpscaleSharpness() const override { return m_upscaleSharpness; }

    void SetRayTracingEnabled(bool enabled) override;
    bool IsRayTracingEnabled() const override;
    void InvalidateRaytracingScene() override;

    void SetAnisotropy(int aniso) override { m_anisotropy = aniso; }
    int GetAnisotropy() const override { return m_anisotropy; }
    void SetVSyncEnabled(bool enabled) override { m_vsyncEnabled = enabled; }
    bool IsVSyncEnabled() const override { return m_vsyncEnabled; }

    // Static scene geometry upload (call once after Initialize)
    void UploadStaticGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Load MSL shaders from preloaded source strings (called from Renderer after loading from PAK)
    void LoadShaders(const std::string& mainShaderSrc, const std::string& skyboxShaderSrc);

    // Helpers
    MetalDevice& GetMetalDevice() { return m_metalDevice; }
    MetalFXSystem& GetMetalFX() { return m_metalFX; }
    MetalRayTracing& GetMetalRayTracing() { return m_rayTracing; }

    std::vector<DynamicModelEntry>& GetDynamicModels() { return m_dynamicModels; }

private:
    void InitPipelines();
    void InitRenderTargets();
    void UpdateConstantBuffers();

    int m_width = 1280;
    int m_height = 720;
    int m_renderWidth = 1280;
    int m_renderHeight = 720;

    Camera* m_camera = nullptr;
    void* m_nativeWindow = nullptr;

    MetalDevice m_metalDevice;
    MetalFXSystem m_metalFX;
    MetalRayTracing m_rayTracing;

    UpscaleTechnique m_upscaleTechnique = UpscaleTechnique::None;
    UpscaleQuality m_upscaleQuality = UpscaleQuality::Quality;
    float m_upscaleSharpness = 0.5f;

    bool m_vsyncEnabled = true;
    int m_anisotropy = 16;
    uint32_t m_frameIndex = 0;

    // Triple Buffering
#ifdef __OBJC__
    CAMetalLayer* m_metalLayer = nil;
    dispatch_semaphore_t m_frameSemaphore = nil;
#else
    void* m_metalLayer = nullptr;
    void* m_frameSemaphore = nullptr;
#endif

    // Textures & Render Targets
    MetalTexture m_sceneColorTex;
    MetalTexture m_velocityTex;
    MetalTexture m_normalTex;
    MetalTexture m_depthTex;
    MetalTexture m_ssaoTex;
    MetalTexture m_ssaoBlurTex;
    MetalTexture m_reflectionTex;
    MetalTexture m_volumetricFogTex;
    MetalTexture m_volumetricFogAccumTex;
    MetalTexture m_upscaledColorTex;

    // Constant Buffers (Unified Memory)
    MetalBuffer m_sceneConstantBuffer[3];
    MetalBuffer m_skyboxConstantBuffer[3];
    MetalBuffer m_ssaoConstantBuffer[3];
    MetalBuffer m_postConstantBuffer[3];

    // Pipelines
#ifdef __OBJC__
    id<MTLRenderPipelineState> m_gbufferPipeline = nil;
    id<MTLRenderPipelineState> m_skyboxPipeline = nil;
    id<MTLRenderPipelineState> m_ssaoPipeline = nil;
    id<MTLRenderPipelineState> m_ssaoBlurPipeline = nil;
    id<MTLComputePipelineState> m_fogInjectPipeline = nil;
    id<MTLComputePipelineState> m_fogAccumPipeline = nil;
    id<MTLComputePipelineState> m_ssrPipeline = nil;
    id<MTLRenderPipelineState> m_compositePipeline = nil;
    id<MTLRenderPipelineState> m_postProcessPipeline = nil;
    id<MTLRenderPipelineState> m_rainStreakPipeline = nil;
    id<MTLRenderPipelineState> m_rainSplashPipeline = nil;

    id<MTLDepthStencilState> m_depthStateGreaterEqual = nil;
    id<MTLDepthStencilState> m_depthStateDisabled = nil;
    id<MTLSamplerState> m_linearSampler = nil;
    id<MTLSamplerState> m_pointSampler = nil;
#else
    void* m_gbufferPipeline = nullptr;
    void* m_skyboxPipeline = nullptr;
    void* m_ssaoPipeline = nullptr;
    void* m_ssaoBlurPipeline = nullptr;
    void* m_fogInjectPipeline = nullptr;
    void* m_fogAccumPipeline = nullptr;
    void* m_ssrPipeline = nullptr;
    void* m_compositePipeline = nullptr;
    void* m_postProcessPipeline = nullptr;
    void* m_rainStreakPipeline = nullptr;
    void* m_rainSplashPipeline = nullptr;
    void* m_depthStateGreaterEqual = nullptr;
    void* m_depthStateDisabled = nullptr;
    void* m_linearSampler = nullptr;
    void* m_pointSampler = nullptr;
#endif

    std::vector<DynamicModelEntry> m_dynamicModels;
    uint32_t m_nextDynamicModelId = 1000;

    // Static scene geometry (uploaded once from Renderer::Initialize)
    MetalBuffer m_staticVertexBuffer;
    MetalBuffer m_staticIndexBuffer;
    uint32_t m_staticIndexCount = 0;
};

} // namespace EngineRender
