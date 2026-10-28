#import "MetalRayTracing.h"
#import <Metal/Metal.h>
#import <iostream>

namespace EngineRender {

MetalRayTracing::MetalRayTracing() = default;

MetalRayTracing::~MetalRayTracing() {
    Shutdown();
}

void MetalRayTracing::Shutdown() {
    @autoreleasepool {
        m_pipelineState = nil;
        m_supported = false;
        m_enabled = false;
    }
}

bool MetalRayTracing::Initialize(id<MTLDevice> device, id<MTLLibrary> library) {
    @autoreleasepool {
        Shutdown();

        if (!device || !library) return false;

        if (@available(macOS 11.0, *)) {
            if (![device supportsRaytracing]) {
                std::cout << "[Metal Ray Tracing] Hardware ray tracing is not supported on this GPU." << std::endl;
                return false;
            }

            id<MTLFunction> rayKernel = [library newFunctionWithName:@"RaytracingKernel"];
            if (!rayKernel) {
                std::cerr << "[Metal Ray Tracing ERROR] Failed to load RaytracingKernel function!" << std::endl;
                return false;
            }

            MTLComputePipelineDescriptor* desc = [[MTLComputePipelineDescriptor alloc] init];
            desc.computeFunction = rayKernel;

            NSError* error = nil;
            m_pipelineState = [device newComputePipelineStateWithDescriptor:desc
                                                                    options:0
                                                                 reflection:nil
                                                                      error:&error];
            if (error) {
                std::cerr << "[Metal Ray Tracing ERROR] Failed to create RT compute pipeline: "
                          << [[error localizedDescription] UTF8String] << std::endl;
                return false;
            }

            m_supported = YES;
            m_enabled = YES;
            std::cout << "[Metal Ray Tracing] Initialized successfully with hardware acceleration!" << std::endl;
            return true;
        }

        return false;
    }
}

id<MTLAccelerationStructure> MetalRayTracing::BuildBLAS(id<MTLDevice> device,
                                                        id<MTLCommandQueue> queue,
                                                        id<MTLBuffer> vertexBuffer,
                                                        id<MTLBuffer> indexBuffer,
                                                        uint32_t triangleCount) {
    @autoreleasepool {
        if (@available(macOS 11.0, *)) {
            if (!m_supported || !device || !queue || !vertexBuffer || !indexBuffer || triangleCount == 0) {
                return nil;
            }

            MTLAccelerationStructureTriangleGeometryDescriptor* geomDesc =
                [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
            geomDesc.vertexBuffer = vertexBuffer;
            geomDesc.vertexStride = sizeof(Vertex);
            geomDesc.indexBuffer = indexBuffer;
            geomDesc.indexType = MTLIndexTypeUInt32;
            geomDesc.triangleCount = triangleCount;

            MTLPrimitiveAccelerationStructureDescriptor* accelDesc =
                [MTLPrimitiveAccelerationStructureDescriptor descriptor];
            accelDesc.geometryDescriptors = @[ geomDesc ];

            MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:accelDesc];
            id<MTLAccelerationStructure> blas = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
            id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                        options:MTLResourceStorageModePrivate];

            id<MTLCommandBuffer> cmd = [queue commandBuffer];
            id<MTLAccelerationStructureCommandEncoder> enc = [cmd accelerationStructureCommandEncoder];
            [enc buildAccelerationStructure:blas descriptor:accelDesc scratchBuffer:scratch scratchBufferOffset:0];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];

            return blas;
        }
        return nil;
    }
}

id<MTLAccelerationStructure> MetalRayTracing::BuildTLAS(id<MTLDevice> device,
                                                        id<MTLCommandQueue> queue,
                                                        id<MTLBuffer> instanceBuffer,
                                                        uint32_t instanceCount) {
    @autoreleasepool {
        if (@available(macOS 11.0, *)) {
            if (!m_supported || !device || !queue || !instanceBuffer || instanceCount == 0) {
                return nil;
            }

            MTLInstanceAccelerationStructureDescriptor* accelDesc =
                [MTLInstanceAccelerationStructureDescriptor descriptor];
            accelDesc.instanceDescriptorBuffer = instanceBuffer;
            accelDesc.instanceCount = instanceCount;

            MTLAccelerationStructureSizes sizes = [device accelerationStructureSizesWithDescriptor:accelDesc];
            id<MTLAccelerationStructure> tlas = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
            id<MTLBuffer> scratch = [device newBufferWithLength:sizes.buildScratchBufferSize
                                                        options:MTLResourceStorageModePrivate];

            id<MTLCommandBuffer> cmd = [queue commandBuffer];
            id<MTLAccelerationStructureCommandEncoder> enc = [cmd accelerationStructureCommandEncoder];
            [enc buildAccelerationStructure:tlas descriptor:accelDesc scratchBuffer:scratch scratchBufferOffset:0];
            [enc endEncoding];
            [cmd commit];
            [cmd waitUntilCompleted];

            return tlas;
        }
        return nil;
    }
}

void MetalRayTracing::DispatchRays(id<MTLCommandBuffer> commandBuffer,
                                   id<MTLAccelerationStructure> tlas,
                                   id<MTLBuffer> constantsBuffer,
                                   id<MTLTexture> outputTexture) {
    @autoreleasepool {
        if (@available(macOS 11.0, *)) {
            if (!m_supported || !m_pipelineState || !tlas || !constantsBuffer || !outputTexture) return;

            id<MTLComputeCommandEncoder> enc = [commandBuffer computeCommandEncoder];
            [enc setLabel:@"Metal Ray Tracing Dispatch"];
            [enc setComputePipelineState:m_pipelineState];
            [enc setBuffer:constantsBuffer offset:0 atIndex:0];
            [enc setAccelerationStructure:tlas atBufferIndex:1];
            [enc setTexture:outputTexture atIndex:0];

            NSUInteger w = [outputTexture width];
            NSUInteger h = [outputTexture height];
            MTLSize threadgroups = MTLSizeMake((w + 15) / 16, (h + 15) / 16, 1);
            MTLSize threadsPerGroup = MTLSizeMake(16, 16, 1);
            [enc dispatchThreadgroups:threadgroups threadsPerThreadgroup:threadsPerGroup];
            [enc endEncoding];
        }
    }
}

} // namespace EngineRender
