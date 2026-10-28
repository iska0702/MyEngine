#pragma once

#include "Renderer/Common/IGraphicsBackend.h"
#include "PS4Device.h"
#include "Platform/PS4/PS4Window.h"
#include <imgui.h>
#include <vector>
#include <memory>
#include <string>

namespace EngineRender {

struct PS4DynamicModelEntry {
    uint32_t id = 0;
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    PBRMaterial material;
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };
    DirectX::XMFLOAT3 scale = { 1, 1, 1 };
    DirectX::XMFLOAT4X4 worldMatrix;
    bool visible = true;
};

struct SceneDrawItem {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    int32_t baseVertex = 0;
    DirectX::XMFLOAT4X4 worldMatrix;
    PBRMaterial material;
    bool visible = true;
};

class PS4RendererBackend : public IGraphicsBackend {
public:
    PS4RendererBackend();
    ~PS4RendererBackend() override;

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

    void SetUpscaleTechnique(UpscaleTechnique technique) override { m_upscaleTechnique = technique; }
    UpscaleTechnique GetUpscaleTechnique() const override { return m_upscaleTechnique; }
    void SetUpscaleQuality(UpscaleQuality quality) override { m_upscaleQuality = quality; }
    UpscaleQuality GetUpscaleQuality() const override { return m_upscaleQuality; }
    void SetUpscaleSharpness(float sharpness) override { m_upscaleSharpness = sharpness; }
    float GetUpscaleSharpness() const override { return m_upscaleSharpness; }

    void SetRayTracingEnabled(bool enabled) override { m_rayTracingEnabled = enabled; }
    bool IsRayTracingEnabled() const override { return m_rayTracingEnabled; }
    void InvalidateRaytracingScene() override {}

    void SetAnisotropy(int aniso) override { m_anisotropy = aniso; }
    int GetAnisotropy() const override { return m_anisotropy; }
    void SetVSyncEnabled(bool enabled) override { m_vsyncEnabled = enabled; }
    bool IsVSyncEnabled() const override { return m_vsyncEnabled; }

    // Upload static scene geometry (Cube, Plane, Sphere, Floor)
    void UploadStaticGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Attach to PS4Window
    void SetPS4Window(EnginePlatform::PS4Window* window) { m_ps4Window = window; }
    EnginePlatform::PS4Window* GetPS4Window() const { return m_ps4Window; }

    PS4Device& GetDevice() { return m_device; }
    std::vector<PS4DynamicModelEntry>& GetDynamicModels() { return m_dynamicModels; }

    void SetSceneDrawItems(std::vector<SceneDrawItem> items) { m_sceneDrawItems = std::move(items); }
    void SetLightDir(const DirectX::XMFLOAT3& dir) { m_lightDir = dir; }
    void SetLightColor(const DirectX::XMFLOAT3& col) { m_lightColor = col; }
    void SetSkyColor(const DirectX::XMFLOAT3& col) { m_skyColor = col; }

    // Atmospheric Clouds, Fog, and Rain Environment Controls
    void SetCloudDensity(float density) { m_cloudDensity = density; }
    float GetCloudDensity() const { return m_cloudDensity; }
    void SetCloudTime(float t) { m_cloudTime = t; }
    float GetCloudTime() const { return m_cloudTime; }
    void SetCloudsEnabled(bool enabled) { m_cloudsEnabled = enabled; }
    bool IsCloudsEnabled() const { return m_cloudsEnabled; }
    void SetRainIntensity(float intensity) { m_rainIntensity = intensity; }
    float GetRainIntensity() const { return m_rainIntensity; }
    void SetFogEnabled(bool enabled) { m_fogEnabled = enabled; }
    bool IsFogEnabled() const { return m_fogEnabled; }
    void SetFogParameters(float nearZ, float farZ, const DirectX::XMFLOAT3& color) {
        m_fogNear = nearZ;
        m_fogFar = farZ;
        m_fogColor = color;
    }

private:
    void RenderPBRScene(uint32_t* frameBuffer, int width, int height, float dt);
    void RenderAtmosphericSkyAndClouds(uint32_t* frameBuffer, int width, int height, const DirectX::XMMATRIX& invViewProj, const DirectX::XMFLOAT3& camPos);
    void RenderRainStreaks(uint32_t* frameBuffer, int width, int height, float dt);
    void DrawMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                  uint32_t startIndex, uint32_t indexCount, int32_t baseVertex,
                  const DirectX::XMMATRIX& worldMatrix, const PBRMaterial& material,
                  uint32_t* frameBuffer, float* depthBuffer, int width, int height,
                  const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& lightDir, const DirectX::XMFLOAT3& camPos);

    void DrawImGui(ImDrawData* drawData, uint32_t* frameBuffer, int width, int height);

    bool m_imguiInitialized = false;
    std::vector<uint8_t> m_fontAtlasPixels;
    int m_fontAtlasWidth = 0;
    int m_fontAtlasHeight = 0;

    PS4Device m_device;
    EnginePlatform::PS4Window* m_ps4Window = nullptr;
    Camera* m_camera = nullptr;

    int m_width = 1920;
    int m_height = 1080;

    UpscaleTechnique m_upscaleTechnique = UpscaleTechnique::None;
    UpscaleQuality m_upscaleQuality = UpscaleQuality::Native;
    float m_upscaleSharpness = 0.5f;
    bool m_rayTracingEnabled = false;
    bool m_vsyncEnabled = true;
    int m_anisotropy = 16;

    std::vector<Vertex> m_staticVertices;
    std::vector<uint32_t> m_staticIndices;
    std::vector<PS4DynamicModelEntry> m_dynamicModels;
    std::vector<SceneDrawItem> m_sceneDrawItems;

    DirectX::XMFLOAT3 m_lightDir = { -0.577f, -0.577f, -0.577f };
    DirectX::XMFLOAT3 m_lightColor = { 1.0f, 0.98f, 0.95f };
    DirectX::XMFLOAT3 m_ambientColor = { 0.15f, 0.18f, 0.25f };
    DirectX::XMFLOAT3 m_skyColor = { 0.2f, 0.4f, 0.8f };

    // Environmental state
    float m_cloudDensity = 1.0f;
    float m_cloudTime = 0.0f;
    bool m_cloudsEnabled = true;
    float m_rainIntensity = 0.0f;
    bool m_fogEnabled = true;
    float m_fogNear = 10.0f;
    float m_fogFar = 350.0f;
    DirectX::XMFLOAT3 m_fogColor = { 0.65f, 0.75f, 0.88f };

    GraphicsCapabilities m_capabilities;
};

} // namespace EngineRender
