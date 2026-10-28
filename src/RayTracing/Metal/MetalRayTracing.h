#pragma once

#include "../../Renderer/Common/RenderTypes.h"
#include <string>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

class MetalRayTracing {
public:
    MetalRayTracing();
    ~MetalRayTracing();

#ifdef __OBJC__
    bool Initialize(id<MTLDevice> device, id<MTLLibrary> library);
    void Shutdown();

    id<MTLAccelerationStructure> BuildBLAS(id<MTLDevice> device,
                                           id<MTLCommandQueue> queue,
                                           id<MTLBuffer> vertexBuffer,
                                           id<MTLBuffer> indexBuffer,
                                           uint32_t triangleCount);

    id<MTLAccelerationStructure> BuildTLAS(id<MTLDevice> device,
                                           id<MTLCommandQueue> queue,
                                           id<MTLBuffer> instanceBuffer,
                                           uint32_t instanceCount);

    void DispatchRays(id<MTLCommandBuffer> commandBuffer,
                      id<MTLAccelerationStructure> tlas,
                      id<MTLBuffer> constantsBuffer,
                      id<MTLTexture> outputTexture);
#endif

    bool IsSupported() const { return m_supported; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled && m_supported; }

private:
    bool m_supported = false;
    bool m_enabled = false;

#ifdef __OBJC__
    id<MTLComputePipelineState> m_pipelineState = nil;
#else
    void* m_pipelineState = nullptr;
#endif
};

} // namespace EngineRender
