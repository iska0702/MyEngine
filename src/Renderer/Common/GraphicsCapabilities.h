#pragma once

#include <string>

namespace EngineRender {

struct GraphicsCapabilities {
    std::string deviceName = "Unknown GPU";
    std::string apiVersion = "Unknown";

    bool SupportsMetalFX = false;
    bool SupportsRayTracing = false;
    bool SupportsArgumentBuffers = true;
    bool SupportsIndirectCommandBuffers = false;
    bool SupportsMeshShaders = false;
    bool SupportsHDR = true;
    bool SupportsBCTextures = true;
    bool SupportsASTC = false;
    bool SupportsMSAA = true;
    bool SupportsWideColor = true;
    bool SupportsMemorylessRenderTargets = false;
    bool SupportsUnifiedMemory = false;

    // Workgroup & Shader sizes
    uint32_t MaxThreadsPerThreadgroup = 1024;
    uint32_t MaxBufferBindingCount = 31;
    uint32_t MaxTextureBindingCount = 128;
    uint32_t MaxSamplerBindingCount = 16;
};

} // namespace EngineRender
