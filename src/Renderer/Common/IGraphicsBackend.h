#pragma once

#include "RenderTypes.h"
#include "GraphicsCapabilities.h"
#include <string>
#include <vector>
#include <memory>

class Camera;
class Texture;

namespace EngineRender {

class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;

    virtual bool Initialize(void* nativeWindowHandle, int width, int height, Camera* camera) = 0;
    virtual void Destroy() = 0;

    virtual void Resize(int width, int height) = 0;
    virtual void BeginUI() = 0;
    virtual void Render(float dt) = 0;
    virtual bool IsDeviceRemoved() const = 0;

    virtual const GraphicsCapabilities& GetCapabilities() const = 0;

    // Dynamic model management
    virtual uint32_t CreateDynamicModel(const std::string& name,
                                       const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       const PBRMaterial& material) = 0;
    virtual bool UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Vertex>& vertices) = 0;
    virtual bool UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) = 0;
    virtual bool RemoveDynamicModel(uint32_t modelId) = 0;

    // Upscaling & Post-processing
    virtual void SetUpscaleTechnique(UpscaleTechnique technique) = 0;
    virtual UpscaleTechnique GetUpscaleTechnique() const = 0;
    virtual void SetUpscaleQuality(UpscaleQuality quality) = 0;
    virtual UpscaleQuality GetUpscaleQuality() const = 0;
    virtual void SetUpscaleSharpness(float sharpness) = 0;
    virtual float GetUpscaleSharpness() const = 0;

    // Ray Tracing
    virtual void SetRayTracingEnabled(bool enabled) = 0;
    virtual bool IsRayTracingEnabled() const = 0;
    virtual void InvalidateRaytracingScene() = 0;

    // Anisotropy & VSync
    virtual void SetAnisotropy(int aniso) = 0;
    virtual int GetAnisotropy() const = 0;
    virtual void SetVSyncEnabled(bool enabled) = 0;
    virtual bool IsVSyncEnabled() const = 0;
};

} // namespace EngineRender
