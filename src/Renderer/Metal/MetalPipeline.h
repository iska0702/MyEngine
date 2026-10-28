#pragma once

#include <string>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

class MetalPipeline {
public:
    MetalPipeline() = default;
    ~MetalPipeline() = default;

#ifdef __OBJC__
    static id<MTLRenderPipelineState> CreateRenderPipeline(
        id<MTLDevice> device,
        id<MTLFunction> vertexFunc,
        id<MTLFunction> fragmentFunc,
        const std::vector<MTLPixelFormat>& colorFormats,
        MTLPixelFormat depthFormat,
        bool blendEnabled = false,
        MTLVertexDescriptor* vertexDesc = nil,
        const std::string& label = "");

    static id<MTLComputePipelineState> CreateComputePipeline(
        id<MTLDevice> device,
        id<MTLFunction> computeFunc,
        const std::string& label = "");

    static id<MTLDepthStencilState> CreateDepthStencilState(
        id<MTLDevice> device,
        MTLCompareFunction compareFunc,
        bool depthWriteEnabled);

    static id<MTLSamplerState> CreateSamplerState(
        id<MTLDevice> device,
        MTLSamplerMinMagFilter filter,
        MTLSamplerAddressMode addressMode,
        uint32_t maxAnisotropy = 1);
#endif
};

} // namespace EngineRender
