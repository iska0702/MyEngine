#import "MetalRendererBackend.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <iostream>
#import <fstream>
#import <sstream>
#import "../Camera.h"
#import "../../Storage/Crypto.h"
#import "../../Storage/PakCompression.h"
#ifndef ENGINE_RELEASE
#import "../../imgui/imgui.h"
#import "../../imgui/imgui_impl_metal.h"
#endif

namespace EngineRender {

MetalRendererBackend::MetalRendererBackend() = default;

MetalRendererBackend::~MetalRendererBackend() {
    Destroy();
}

const GraphicsCapabilities& MetalRendererBackend::GetCapabilities() const {
    return m_metalDevice.GetCapabilities();
}

bool MetalRendererBackend::Initialize(void* nativeWindowHandle, int width, int height, Camera* camera) {
    @autoreleasepool {
        m_width = width;
        m_height = height;
        m_renderWidth = width;
        m_renderHeight = height;
        m_camera = camera;
        m_nativeWindow = nativeWindowHandle;

        if (!m_metalDevice.Initialize()) {
            return false;
        }

        m_metalLayer = (__bridge CAMetalLayer*)nativeWindowHandle;
        if (m_metalLayer) {
            m_metalLayer.device = m_metalDevice.GetDevice();
            m_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        }

        m_frameSemaphore = dispatch_semaphore_create(3);

        InitRenderTargets();
        InitPipelines();

        // Initialize MetalFX
        m_metalFX.Initialize(m_metalDevice.GetDevice(),
                             m_renderWidth, m_renderHeight,
                             m_width, m_height,
                             MetalFXMode::Disabled, MetalFXQuality::Quality);

        // Initialize Metal Ray Tracing
        id<MTLLibrary> defaultLib = m_metalDevice.GetDefaultLibrary();
        if (defaultLib) {
            m_rayTracing.Initialize(m_metalDevice.GetDevice(), defaultLib);
        }

        // Initialize Constant Buffers (Triple-buffered Unified Memory)
        for (int i = 0; i < 3; ++i) {
            m_sceneConstantBuffer[i].Initialize(m_metalDevice.GetDevice(),
                                                sizeof(SceneConstantBuffer),
                                                BufferStorageMode::Shared,
                                                nullptr,
                                                "SceneConstantBuffer");
        }

        std::cout << "[Metal Renderer] Backend initialized successfully!" << std::endl;
        return true;
    }
}

void MetalRendererBackend::InitRenderTargets() {
    @autoreleasepool {
        id<MTLDevice> dev = m_metalDevice.GetDevice();

        NSUInteger usageRT = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        NSUInteger usageUAV = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

        m_sceneColorTex.Initialize2D(dev, m_renderWidth, m_renderHeight, PixelFormat::RGBA16_FLOAT, usageRT, false, "SceneColor");
        m_velocityTex.Initialize2D(dev, m_renderWidth, m_renderHeight, PixelFormat::RGBA16_FLOAT, usageRT, false, "Velocity");
        m_normalTex.Initialize2D(dev, m_renderWidth, m_renderHeight, PixelFormat::RGBA16_FLOAT, usageRT, false, "Normal");
        m_depthTex.Initialize2D(dev, m_renderWidth, m_renderHeight, PixelFormat::D32_FLOAT, usageRT, false, "DepthStencil");

        m_ssaoTex.Initialize2D(dev, m_renderWidth / 2, m_renderHeight / 2, PixelFormat::R8_UNORM, usageRT, false, "SSAO");
        m_ssaoBlurTex.Initialize2D(dev, m_renderWidth / 2, m_renderHeight / 2, PixelFormat::R8_UNORM, usageRT, false, "SSAOBlur");

        m_reflectionTex.Initialize2D(dev, m_renderWidth / 2, m_renderHeight / 2, PixelFormat::RGBA16_FLOAT, usageUAV, false, "SSR");
        m_volumetricFogTex.Initialize3D(dev, 160, 90, 128, PixelFormat::RGBA16_FLOAT, usageUAV, "FogVolume");
        m_volumetricFogAccumTex.Initialize3D(dev, 160, 90, 128, PixelFormat::RGBA16_FLOAT, usageUAV, "FogAccum");

        m_upscaledColorTex.Initialize2D(dev, m_width, m_height, PixelFormat::RGBA16_FLOAT, usageRT, false, "UpscaledColor");
    }
}

static std::string LoadShaderSourceFromPak(const std::string& shaderFilename) {
    // Try to find and read a .metal file from Contents/contentXX.pak (relative to executable)
    std::vector<std::string> basePaths = {
        "Contents/",
        "../Resources/Contents/",
    };
    for (const auto& base : basePaths) {
        for (int i = 1; i <= 10; ++i) {
            char pakPath[512];
            snprintf(pakPath, sizeof(pakPath), "%scontent%02d.pak", base.c_str(), i);
            std::ifstream f(pakPath, std::ios::binary);
            if (!f.is_open()) continue;

            uint32_t magic = 0;
            f.read((char*)&magic, 4);
            if (magic != 0x4B415045) continue; // 'EPAK'

            uint8_t indexIV[16];
            f.read((char*)indexIV, 16);
            uint32_t indexCipherSize = 0, indexPlainSize = 0;
            f.read((char*)&indexCipherSize, 4);
            f.read((char*)&indexPlainSize, 4);
            if (indexCipherSize == 0 || indexCipherSize > 100*1024*1024) continue;

            std::vector<uint8_t> masterKey = PakCrypto::GetMasterKey();
            std::vector<uint8_t> indexCipher(indexCipherSize);
            f.read((char*)indexCipher.data(), indexCipherSize);
            std::vector<uint8_t> plainIndex;
            if (!PakCrypto::DecryptAES256CBC(masterKey.data(), masterKey.size(),
                                              indexIV, 16,
                                              indexCipher.data(), indexCipher.size(),
                                              plainIndex) || plainIndex.size() < 4) continue;

            uint32_t numFiles = *(uint32_t*)plainIndex.data();
            size_t entryOffset = 4;
            for (uint32_t j = 0; j < numFiles; ++j) {
                if (entryOffset + 276 > plainIndex.size()) break;
                wchar_t nameBuffer[129];
                memcpy(nameBuffer, plainIndex.data() + entryOffset, 256);
                nameBuffer[128] = L'\0';
                entryOffset += 256;
                uint32_t offset      = *(uint32_t*)(plainIndex.data() + entryOffset); entryOffset += 4;
                uint32_t cipherTotal = *(uint32_t*)(plainIndex.data() + entryOffset); entryOffset += 4;
                uint32_t compLen     = *(uint32_t*)(plainIndex.data() + entryOffset); entryOffset += 4;
                uint32_t uncompLen   = *(uint32_t*)(plainIndex.data() + entryOffset); entryOffset += 4;
                uint32_t flags       = *(uint32_t*)(plainIndex.data() + entryOffset); entryOffset += 4;

                // Convert wide name to UTF-8 for comparison
                std::string narrowName;
                for (int k = 0; nameBuffer[k] && k < 128; k++) {
                    narrowName += (char)nameBuffer[k];
                }
                // Strip trailing spaces
                while (!narrowName.empty() && narrowName.back() == ' ') narrowName.pop_back();

                // Check if ends with our shader filename
                bool match = false;
                if (narrowName.size() >= shaderFilename.size()) {
                    std::string tail = narrowName.substr(narrowName.size() - shaderFilename.size());
                    match = (tail == shaderFilename);
                }
                if (!match) continue;

                if (cipherTotal <= 16) continue;
                f.seekg(offset);
                uint8_t fileIV[16];
                f.read((char*)fileIV, 16);
                uint32_t cipherLen = cipherTotal - 16;
                std::vector<uint8_t> cipherData(cipherLen);
                f.read((char*)cipherData.data(), cipherLen);
                std::vector<uint8_t> decrypted;
                if (!PakCrypto::DecryptAES256CBC(masterKey.data(), masterKey.size(),
                                                  fileIV, 16,
                                                  cipherData.data(), cipherData.size(),
                                                  decrypted)) continue;
                if (flags & 1) {
                    std::vector<uint8_t> uncompressed;
                    if (PakCompression::DecompressDeflate(decrypted.data(), decrypted.size(), uncompLen, uncompressed)) {
                        return std::string(uncompressed.begin(), uncompressed.end());
                    }
                    continue;
                }
                return std::string(decrypted.begin(), decrypted.end());
            }
        }
    }
    return "";
}

void MetalRendererBackend::InitPipelines() {
    @autoreleasepool {
        id<MTLDevice> dev = m_metalDevice.GetDevice();

        m_depthStateGreaterEqual = MetalPipeline::CreateDepthStencilState(dev, MTLCompareFunctionLessEqual, YES);
        m_depthStateDisabled = MetalPipeline::CreateDepthStencilState(dev, MTLCompareFunctionAlways, NO);

        m_linearSampler = MetalPipeline::CreateSamplerState(dev, MTLSamplerMinMagFilterLinear, MTLSamplerAddressModeClampToEdge, m_anisotropy);
        m_pointSampler = MetalPipeline::CreateSamplerState(dev, MTLSamplerMinMagFilterNearest, MTLSamplerAddressModeClampToEdge, 1);

        // Load & compile main shaders from PAK
        std::string shaderSrc = LoadShaderSourceFromPak("shaders.metal");
        std::string skyboxSrc = LoadShaderSourceFromPak("skybox.metal");

        if (!shaderSrc.empty()) {
            std::string err;
            id<MTLLibrary> lib = m_metalDevice.CompileSource(shaderSrc, err);
            if (lib) {
                id<MTLFunction> vsFunc = [lib newFunctionWithName:@"VSMain"];
                id<MTLFunction> fsFunc = [lib newFunctionWithName:@"PSMain"];
                if (vsFunc && fsFunc) {
                    // Vertex descriptor matching VertexIn in shaders.metal
                    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
                    vd.attributes[0].format = MTLVertexFormatFloat3; // position
                    vd.attributes[0].offset = 0;
                    vd.attributes[0].bufferIndex = 0;
                    vd.attributes[1].format = MTLVertexFormatFloat3; // normal
                    vd.attributes[1].offset = sizeof(float)*3;
                    vd.attributes[1].bufferIndex = 0;
                    vd.attributes[2].format = MTLVertexFormatFloat4; // color
                    vd.attributes[2].offset = sizeof(float)*6;
                    vd.attributes[2].bufferIndex = 0;
                    vd.layouts[0].stride = sizeof(float)*10; // pos(3)+normal(3)+color(4)
                    vd.layouts[0].stepRate = 1;
                    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

                    m_gbufferPipeline = MetalPipeline::CreateRenderPipeline(
                        dev, vsFunc, fsFunc,
                        { MTLPixelFormatBGRA8Unorm, MTLPixelFormatRGBA16Float, MTLPixelFormatRGBA16Float },
                        MTLPixelFormatDepth32Float,
                        false, vd, "GBuffer");
                    std::cout << "[Metal] GBuffer pipeline: " << (m_gbufferPipeline ? "OK" : "FAIL") << std::endl;
                }
            } else {
                std::cerr << "[Metal] shaders.metal compile error: " << err << std::endl;
            }
        } else {
            std::cerr << "[Metal] shaders.metal not found in PAK" << std::endl;
        }

        if (!skyboxSrc.empty()) {
            std::string err;
            id<MTLLibrary> skyLib = m_metalDevice.CompileSource(skyboxSrc, err);
            if (skyLib) {
                id<MTLFunction> vsFunc = [skyLib newFunctionWithName:@"VSMain"];
                id<MTLFunction> fsFunc = [skyLib newFunctionWithName:@"PSMain"];
                if (vsFunc && fsFunc) {
                    m_skyboxPipeline = MetalPipeline::CreateRenderPipeline(
                        dev, vsFunc, fsFunc,
                        { MTLPixelFormatBGRA8Unorm },
                        MTLPixelFormatInvalid,
                        false, nil, "Skybox");
                    std::cout << "[Metal] Skybox pipeline: " << (m_skyboxPipeline ? "OK" : "FAIL") << std::endl;
                }
            } else {
                std::cerr << "[Metal] skybox.metal compile error: " << err << std::endl;
            }
        } else {
            std::cerr << "[Metal] skybox.metal not found in PAK" << std::endl;
        }
    }
}

void MetalRendererBackend::Destroy() {
    @autoreleasepool {
        m_metalFX.Shutdown();
        m_rayTracing.Shutdown();

        for (int i = 0; i < 3; ++i) {
            m_sceneConstantBuffer[i].Destroy();
            m_skyboxConstantBuffer[i].Destroy();
            m_ssaoConstantBuffer[i].Destroy();
            m_postConstantBuffer[i].Destroy();
        }

        m_sceneColorTex.Destroy();
        m_velocityTex.Destroy();
        m_normalTex.Destroy();
        m_depthTex.Destroy();
        m_ssaoTex.Destroy();
        m_ssaoBlurTex.Destroy();
        m_reflectionTex.Destroy();
        m_volumetricFogTex.Destroy();
        m_volumetricFogAccumTex.Destroy();
        m_upscaledColorTex.Destroy();

        m_metalDevice.Shutdown();
    }
}

void MetalRendererBackend::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;
    m_renderWidth = width;
    m_renderHeight = height;

    InitRenderTargets();
    m_metalFX.Initialize(m_metalDevice.GetDevice(),
                         m_renderWidth, m_renderHeight,
                         m_width, m_height,
                         m_metalFX.GetMode(), m_metalFX.GetQuality());
}

void MetalRendererBackend::BeginUI() {
    // Handled in ImGui_ImplMetal
}

void MetalRendererBackend::UploadStaticGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;
    id<MTLDevice> dev = m_metalDevice.GetDevice();
    m_staticVertexBuffer.Initialize(dev, vertices.size() * sizeof(Vertex), BufferStorageMode::Shared, vertices.data(), "StaticVB");
    m_staticIndexBuffer.Initialize(dev, indices.size() * sizeof(uint32_t), BufferStorageMode::Shared, indices.data(), "StaticIB");
    m_staticIndexCount = (uint32_t)indices.size();
    std::cout << "[Metal] Uploaded static geometry: " << vertices.size() << " verts, " << indices.size() << " indices" << std::endl;
}

void MetalRendererBackend::Render(float dt) {
    @autoreleasepool {
        if (!m_metalLayer) return;

        dispatch_semaphore_wait(m_frameSemaphore, DISPATCH_TIME_FOREVER);

        id<CAMetalDrawable> drawable = [m_metalLayer nextDrawable];
        if (!drawable) {
            dispatch_semaphore_signal(m_frameSemaphore);
            return;
        }

        id<MTLCommandBuffer> cmd = [m_metalDevice.GetCommandQueue() commandBuffer];
        [cmd setLabel:@"Frame Command Buffer"];

        uint32_t bufferIdx = m_frameIndex % 3;

        // Update Scene Constant Buffer
        SceneConstantBuffer scb = {};
        if (m_camera) {
            XMMATRIX view = m_camera->GetViewMatrix();
            XMMATRIX proj = m_camera->GetProjectionMatrix(60.0f, float(m_width) / std::max(m_height, 1), 0.1f, 1000.0f);
            scb.viewProj = view * proj;
            scb.cleanViewProj = scb.viewProj;
            scb.prevViewProj = scb.viewProj;
            scb.prevCleanViewProj = scb.viewProj;
            scb.model = XMMatrixIdentity();
            scb.cameraPos = m_camera->GetPosition();
            scb.screenRes = XMFLOAT2(float(m_width), float(m_height));
            scb.lightDir = XMFLOAT3(0.3f, -0.7f, 0.5f);
            scb.lightColor = XMFLOAT3(1.0f, 0.98f, 0.95f);
            scb.matAlbedo = XMFLOAT3(0.8f, 0.8f, 0.8f);
            scb.matRoughness = 0.7f;
            scb.matMetallic = 0.0f;
            scb.matReflectance = 0.04f;
            scb.uvTiling = XMFLOAT2(1.0f, 1.0f);
            m_sceneConstantBuffer[bufferIdx].UpdateData(&scb, sizeof(scb));
        }

        // --- PASS 1: Skybox (renders to swapchain directly) ---
        {
            MTLRenderPassDescriptor* skyPass = [MTLRenderPassDescriptor renderPassDescriptor];
            skyPass.colorAttachments[0].texture = drawable.texture;
            skyPass.colorAttachments[0].loadAction = MTLLoadActionClear;
            skyPass.colorAttachments[0].storeAction = MTLStoreActionStore;
            skyPass.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.12, 0.20, 1.0);

            id<MTLRenderCommandEncoder> skyEnc = [cmd renderCommandEncoderWithDescriptor:skyPass];
            [skyEnc setLabel:@"Skybox Pass"];

            if (m_skyboxPipeline && m_camera) {
                // Build invViewProj for skybox
                XMMATRIX view = m_camera->GetViewMatrix();
                XMMATRIX proj = m_camera->GetProjectionMatrix(60.0f, float(m_width) / std::max(m_height, 1), 0.1f, 1000.0f);
                // Remove translation from view for skybox
                XMMATRIX viewNoTrans = view;
                viewNoTrans.r[3] = XMVectorSet(0, 0, 0, 1);
                XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewNoTrans * proj);

                struct SkyboxCB {
                    XMFLOAT4X4 invViewProj;
                    XMFLOAT4 lightColor;
                    XMFLOAT4 lightDir;
                    XMFLOAT4 skyColor;
                } skyCB = {};
                XMStoreFloat4x4(&skyCB.invViewProj, XMMatrixTranspose(invViewProj));
                skyCB.lightColor = XMFLOAT4(1.0f, 0.98f, 0.95f, 1.0f);
                skyCB.lightDir = XMFLOAT4(0.3f, -0.7f, 0.5f, 0.0f);
                skyCB.skyColor = XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);

                [skyEnc setRenderPipelineState:m_skyboxPipeline];
                [skyEnc setVertexBytes:&skyCB length:sizeof(skyCB) atIndex:0];
                [skyEnc setFragmentBytes:&skyCB length:sizeof(skyCB) atIndex:0];
                [skyEnc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }

            [skyEnc endEncoding];
        }

        // --- PASS 2: Scene geometry (G-buffer pass, write to swapchain on top of skybox) ---
        if (m_gbufferPipeline && m_staticVertexBuffer.GetBuffer() && m_staticIndexBuffer.GetBuffer() && m_staticIndexCount > 0) {
            MTLRenderPassDescriptor* geoPass = [MTLRenderPassDescriptor renderPassDescriptor];
            geoPass.colorAttachments[0].texture = drawable.texture;
            geoPass.colorAttachments[0].loadAction = MTLLoadActionLoad;  // preserve skybox
            geoPass.colorAttachments[0].storeAction = MTLStoreActionStore;
            // velocity + normal attachments (unused but required by pipeline)
            geoPass.colorAttachments[1].texture = m_velocityTex.GetTexture();
            geoPass.colorAttachments[1].loadAction = MTLLoadActionDontCare;
            geoPass.colorAttachments[1].storeAction = MTLStoreActionDontCare;
            geoPass.colorAttachments[2].texture = m_normalTex.GetTexture();
            geoPass.colorAttachments[2].loadAction = MTLLoadActionDontCare;
            geoPass.colorAttachments[2].storeAction = MTLStoreActionDontCare;
            geoPass.depthAttachment.texture = m_depthTex.GetTexture();
            geoPass.depthAttachment.loadAction = MTLLoadActionClear;
            geoPass.depthAttachment.storeAction = MTLStoreActionDontCare;
            geoPass.depthAttachment.clearDepth = 1.0;

            id<MTLRenderCommandEncoder> geoEnc = [cmd renderCommandEncoderWithDescriptor:geoPass];
            [geoEnc setLabel:@"Scene Geometry Pass"];
            [geoEnc setRenderPipelineState:m_gbufferPipeline];
            [geoEnc setDepthStencilState:m_depthStateGreaterEqual];
            [geoEnc setCullMode:MTLCullModeBack];
            [geoEnc setVertexBuffer:m_staticVertexBuffer.GetBuffer() offset:0 atIndex:0];
            [geoEnc setVertexBuffer:m_sceneConstantBuffer[bufferIdx].GetBuffer() offset:0 atIndex:1];
            [geoEnc setFragmentBuffer:m_sceneConstantBuffer[bufferIdx].GetBuffer() offset:0 atIndex:1];
            [geoEnc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                               indexCount:m_staticIndexCount
                                indexType:MTLIndexTypeUInt32
                              indexBuffer:m_staticIndexBuffer.GetBuffer()
                        indexBufferOffset:0];
            [geoEnc endEncoding];
        }

#ifndef ENGINE_RELEASE
        // --- PASS 3: ImGui overlay ---
        {
            MTLRenderPassDescriptor* imguiPass = [MTLRenderPassDescriptor renderPassDescriptor];
            imguiPass.colorAttachments[0].texture = drawable.texture;
            imguiPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
            imguiPass.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> imguiEnc = [cmd renderCommandEncoderWithDescriptor:imguiPass];
            [imguiEnc setLabel:@"ImGui Pass"];
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData) {
                ImGui_ImplMetal_RenderDrawData(drawData, (__bridge void*)cmd, (__bridge void*)imguiEnc);
            }
            [imguiEnc endEncoding];
        }
#endif

        [cmd presentDrawable:drawable];

        __block dispatch_semaphore_t sem = m_frameSemaphore;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
            dispatch_semaphore_signal(sem);
        }];

        [cmd commit];
        m_frameIndex++;
    }
}

uint32_t MetalRendererBackend::CreateDynamicModel(const std::string& name,
                                                  const std::vector<Vertex>& vertices,
                                                  const std::vector<uint32_t>& indices,
                                                  const PBRMaterial& material) {
    DynamicModelEntry entry;
    entry.id = m_nextDynamicModelId++;
    entry.name = name;
    entry.vertexCount = (uint32_t)vertices.size();
    entry.indexCount = (uint32_t)indices.size();
    entry.material = material;

    id<MTLDevice> dev = m_metalDevice.GetDevice();
    entry.vertexBuffer.Initialize(dev, vertices.size() * sizeof(Vertex), BufferStorageMode::Shared, vertices.data(), name + "_VB");
    entry.indexBuffer.Initialize(dev, indices.size() * sizeof(uint32_t), BufferStorageMode::Shared, indices.data(), name + "_IB");

    m_dynamicModels.push_back(std::move(entry));
    return entry.id;
}

bool MetalRendererBackend::UpdateDynamicModelVertexBuffer(uint32_t modelId, const std::vector<Vertex>& vertices) {
    for (auto& m : m_dynamicModels) {
        if (m.id == modelId) {
            m.vertexBuffer.UpdateData(vertices.data(), vertices.size() * sizeof(Vertex));
            return true;
        }
    }
    return false;
}

bool MetalRendererBackend::UpdateDynamicModelTopology(uint32_t modelId, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    for (auto& m : m_dynamicModels) {
        if (m.id == modelId) {
            id<MTLDevice> dev = m_metalDevice.GetDevice();
            m.vertexCount = (uint32_t)vertices.size();
            m.indexCount = (uint32_t)indices.size();
            m.vertexBuffer.Initialize(dev, vertices.size() * sizeof(Vertex), BufferStorageMode::Shared, vertices.data());
            m.indexBuffer.Initialize(dev, indices.size() * sizeof(uint32_t), BufferStorageMode::Shared, indices.data());
            return true;
        }
    }
    return false;
}

bool MetalRendererBackend::RemoveDynamicModel(uint32_t modelId) {
    for (auto it = m_dynamicModels.begin(); it != m_dynamicModels.end(); ++it) {
        if (it->id == modelId) {
            it->vertexBuffer.Destroy();
            it->indexBuffer.Destroy();
            m_dynamicModels.erase(it);
            return true;
        }
    }
    return false;
}

void MetalRendererBackend::SetUpscaleTechnique(UpscaleTechnique technique) {
    m_upscaleTechnique = technique;
}

void MetalRendererBackend::SetUpscaleQuality(UpscaleQuality quality) {
    m_upscaleQuality = quality;
}

void MetalRendererBackend::SetUpscaleSharpness(float sharpness) {
    m_upscaleSharpness = sharpness;
    m_metalFX.SetSharpness(sharpness);
}

void MetalRendererBackend::SetRayTracingEnabled(bool enabled) {
    m_rayTracing.SetEnabled(enabled);
}

bool MetalRendererBackend::IsRayTracingEnabled() const {
    return m_rayTracing.IsEnabled();
}

void MetalRendererBackend::InvalidateRaytracingScene() {
}

} // namespace EngineRender
