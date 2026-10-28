#pragma once

#include "../../Renderer/Common/RenderTypes.h"
#include <string>
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
typedef void* id;
#endif

namespace EngineRender {

class MetalFXSystem {
public:
    MetalFXSystem();
    ~MetalFXSystem();

#ifdef __OBJC__
    bool Initialize(id<MTLDevice> device,
                    int inputWidth, int inputHeight,
                    int outputWidth, int outputHeight,
                    MetalFXMode mode, MetalFXQuality quality);

    void EncodeSpatial(id<MTLCommandBuffer> commandBuffer,
                       id<MTLTexture> inputColor,
                       id<MTLTexture> outputColor);

    void EncodeTemporal(id<MTLCommandBuffer> commandBuffer,
                        id<MTLTexture> inputColor,
                        id<MTLTexture> velocity,
                        id<MTLTexture> depth,
                        id<MTLTexture> outputColor,
                        float jitterX, float jitterY,
                        bool resetHistory);
#endif

    void Shutdown();

    void SetMode(MetalFXMode mode) { m_mode = mode; }
    MetalFXMode GetMode() const { return m_mode; }

    void SetQuality(MetalFXQuality quality) { m_quality = quality; }
    MetalFXQuality GetQuality() const { return m_quality; }

    void SetSharpness(float sharpness) { m_sharpness = sharpness; }
    float GetSharpness() const { return m_sharpness; }

    bool IsSupported() const { return m_supported; }

private:
    bool m_supported = false;
    MetalFXMode m_mode = MetalFXMode::Disabled;
    MetalFXQuality m_quality = MetalFXQuality::Quality;
    float m_sharpness = 0.5f;

    int m_inputWidth = 0;
    int m_inputHeight = 0;
    int m_outputWidth = 0;
    int m_outputHeight = 0;

#ifdef __OBJC__
    id m_spatialScaler = nil;
    id m_temporalScaler = nil;
#else
    void* m_spatialScaler = nullptr;
    void* m_temporalScaler = nullptr;
#endif
};

} // namespace EngineRender
