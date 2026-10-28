#import "MetalFXSystem.h"
#import <Metal/Metal.h>

#if __has_include(<MetalFX/MetalFX.h>)
#import <MetalFX/MetalFX.h>
#define HAS_METALLIC_FX 1
#else
#define HAS_METALLIC_FX 0
#endif

#import <iostream>

namespace EngineRender {

MetalFXSystem::MetalFXSystem() = default;

MetalFXSystem::~MetalFXSystem() {
    Shutdown();
}

void MetalFXSystem::Shutdown() {
    @autoreleasepool {
        m_spatialScaler = nil;
        m_temporalScaler = nil;
        m_supported = false;
    }
}

bool MetalFXSystem::Initialize(id<MTLDevice> device,
                               int inputWidth, int inputHeight,
                               int outputWidth, int outputHeight,
                               MetalFXMode mode, MetalFXQuality quality) {
    @autoreleasepool {
        Shutdown();

        m_inputWidth = inputWidth;
        m_inputHeight = inputHeight;
        m_outputWidth = outputWidth;
        m_outputHeight = outputHeight;
        m_mode = mode;
        m_quality = quality;

#if HAS_METALLIC_FX
        if (@available(macOS 13.0, *)) {
            // Check Spatial Scaler Support
            MTLFXSpatialScalerDescriptor* spatialDesc = [[MTLFXSpatialScalerDescriptor alloc] init];
            spatialDesc.colorTextureFormat = MTLPixelFormatRGBA16Float;
            spatialDesc.outputTextureFormat = MTLPixelFormatRGBA16Float;
            spatialDesc.inputWidth = inputWidth;
            spatialDesc.inputHeight = inputHeight;
            spatialDesc.outputWidth = outputWidth;
            spatialDesc.outputHeight = outputHeight;
            spatialDesc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;

            m_spatialScaler = [spatialDesc newSpatialScalerWithDevice:device];

            // Check Temporal Scaler Support
            MTLFXTemporalScalerDescriptor* temporalDesc = [[MTLFXTemporalScalerDescriptor alloc] init];
            temporalDesc.colorTextureFormat = MTLPixelFormatRGBA16Float;
            temporalDesc.depthTextureFormat = MTLPixelFormatDepth32Float;
            temporalDesc.motionTextureFormat = MTLPixelFormatRGBA16Float;
            temporalDesc.outputTextureFormat = MTLPixelFormatRGBA16Float;
            temporalDesc.inputWidth = inputWidth;
            temporalDesc.inputHeight = inputHeight;
            temporalDesc.outputWidth = outputWidth;
            temporalDesc.outputHeight = outputHeight;

            m_temporalScaler = [temporalDesc newTemporalScalerWithDevice:device];
            m_supported = (m_spatialScaler != nil || m_temporalScaler != nil);

            std::cout << "[MetalFX] Scalers Initialized ("
                      << inputWidth << "x" << inputHeight << " -> "
                      << outputWidth << "x" << outputHeight << "): "
                      << (m_supported ? "SUPPORTED" : "NOT SUPPORTED") << std::endl;

            return m_supported;
        }
#endif
        return false;
    }
}

void MetalFXSystem::EncodeSpatial(id<MTLCommandBuffer> commandBuffer,
                                  id<MTLTexture> inputColor,
                                  id<MTLTexture> outputColor) {
    @autoreleasepool {
#if HAS_METALLIC_FX
        if (@available(macOS 13.0, *)) {
            if (m_spatialScaler && inputColor && outputColor) {
                id<MTLFXSpatialScaler> scaler = (id<MTLFXSpatialScaler>)m_spatialScaler;
                [scaler setColorTexture:inputColor];
                [scaler setOutputTexture:outputColor];
                [scaler encodeToCommandBuffer:commandBuffer];
            }
        }
#endif
    }
}

void MetalFXSystem::EncodeTemporal(id<MTLCommandBuffer> commandBuffer,
                                   id<MTLTexture> inputColor,
                                   id<MTLTexture> velocity,
                                   id<MTLTexture> depth,
                                   id<MTLTexture> outputColor,
                                   float jitterX, float jitterY,
                                   bool resetHistory) {
    @autoreleasepool {
#if HAS_METALLIC_FX
        if (@available(macOS 13.0, *)) {
            if (m_temporalScaler && inputColor && outputColor && velocity && depth) {
                id<MTLFXTemporalScaler> scaler = (id<MTLFXTemporalScaler>)m_temporalScaler;
                [scaler setColorTexture:inputColor];
                [scaler setMotionTexture:velocity];
                [scaler setDepthTexture:depth];
                [scaler setOutputTexture:outputColor];
                [scaler setJitterOffsetX:jitterX];
                [scaler setJitterOffsetY:jitterY];
                [scaler setReset:resetHistory];
                [scaler encodeToCommandBuffer:commandBuffer];
            }
        }
#endif
    }
}

} // namespace EngineRender
