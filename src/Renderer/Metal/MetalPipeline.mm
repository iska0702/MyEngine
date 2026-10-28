#import "MetalPipeline.h"
#import <Metal/Metal.h>
#import <iostream>

namespace EngineRender {

id<MTLRenderPipelineState> MetalPipeline::CreateRenderPipeline(
    id<MTLDevice> device,
    id<MTLFunction> vertexFunc,
    id<MTLFunction> fragmentFunc,
    const std::vector<MTLPixelFormat>& colorFormats,
    MTLPixelFormat depthFormat,
    bool blendEnabled,
    MTLVertexDescriptor* vertexDesc,
    const std::string& label) {
    @autoreleasepool {
        if (!device || !vertexFunc || !fragmentFunc) return nil;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertexFunc;
        desc.fragmentFunction = fragmentFunc;
        if (vertexDesc) desc.vertexDescriptor = vertexDesc;

        for (size_t i = 0; i < colorFormats.size(); ++i) {
            desc.colorAttachments[i].pixelFormat = colorFormats[i];
            if (blendEnabled && i == 0) {
                desc.colorAttachments[i].blendingEnabled = YES;
                desc.colorAttachments[i].rgbBlendOperation = MTLBlendOperationAdd;
                desc.colorAttachments[i].alphaBlendOperation = MTLBlendOperationAdd;
                desc.colorAttachments[i].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                desc.colorAttachments[i].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                desc.colorAttachments[i].sourceAlphaBlendFactor = MTLBlendFactorOne;
                desc.colorAttachments[i].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            }
        }

        desc.depthAttachmentPixelFormat = depthFormat;
        if (!label.empty()) {
            [desc setLabel:[NSString stringWithUTF8String:label.c_str()]];
        }

        NSError* error = nil;
        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (error) {
            std::cerr << "[Metal Render Pipeline Error]: " << [[error localizedDescription] UTF8String] << std::endl;
        }
        return pso;
    }
}

id<MTLComputePipelineState> MetalPipeline::CreateComputePipeline(
    id<MTLDevice> device,
    id<MTLFunction> computeFunc,
    const std::string& label) {
    @autoreleasepool {
        if (!device || !computeFunc) return nil;

        NSError* error = nil;
        id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:computeFunc error:&error];
        if (error) {
            std::cerr << "[Metal Compute Pipeline Error]: " << [[error localizedDescription] UTF8String] << std::endl;
        }
        return pso;
    }
}

id<MTLDepthStencilState> MetalPipeline::CreateDepthStencilState(
    id<MTLDevice> device,
    MTLCompareFunction compareFunc,
    bool depthWriteEnabled) {
    @autoreleasepool {
        if (!device) return nil;

        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = compareFunc;
        desc.depthWriteEnabled = depthWriteEnabled;
        return [device newDepthStencilStateWithDescriptor:desc];
    }
}

id<MTLSamplerState> MetalPipeline::CreateSamplerState(
    id<MTLDevice> device,
    MTLSamplerMinMagFilter filter,
    MTLSamplerAddressMode addressMode,
    uint32_t maxAnisotropy) {
    @autoreleasepool {
        if (!device) return nil;

        MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];
        desc.minFilter = filter;
        desc.magFilter = filter;
        desc.mipFilter = (filter == MTLSamplerMinMagFilterLinear) ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        desc.sAddressMode = addressMode;
        desc.tAddressMode = addressMode;
        desc.rAddressMode = addressMode;
        desc.maxAnisotropy = maxAnisotropy;
        return [device newSamplerStateWithDescriptor:desc];
    }
}

} // namespace EngineRender
