#import "imgui_impl_metal.h"
#import <Metal/Metal.h>
#import <simd/simd.h>

struct MetalBufferChunk {
    id<MTLBuffer> buffer;
    size_t offset;
};

static id<MTLDevice> g_Device = nil;
static id<MTLRenderPipelineState> g_PipelineState = nil;
static id<MTLDepthStencilState> g_DepthStencilState = nil;
static id<MTLTexture> g_FontTexture = nil;
static id<MTLSamplerState> g_SamplerState = nil;

static const char* g_ShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float4x4 projectionMatrix;
};

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    uchar4 color    [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    out.position = uniforms.projectionMatrix * float4(in.position, 0.0, 1.0);
    out.uv = in.uv;
    out.color = float4(in.color) / 255.0;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              texture2d<float> tex [[texture(0)]],
                              sampler samp [[sampler(0)]]) {
    return in.color * tex.sample(samp, in.uv);
}
)";

bool ImGui_ImplMetal_Init(void* device) {
    g_Device = (__bridge id<MTLDevice>)device;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_metal";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    return ImGui_ImplMetal_CreateDeviceObjects(device);
}

void ImGui_ImplMetal_Shutdown() {
    ImGui_ImplMetal_DestroyDeviceObjects();
    g_Device = nil;
}

void ImGui_ImplMetal_NewFrame(void* renderPassDescriptor) {
    if (!g_FontTexture) {
        ImGui_ImplMetal_CreateFontsTexture((__bridge void*)g_Device);
    }
}

bool ImGui_ImplMetal_CreateFontsTexture(void* device) {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                    width:width
                                                                                   height:height
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeManaged;

    g_FontTexture = [g_Device newTextureWithDescriptor:desc];
    [g_FontTexture replaceRegion:MTLRegionMake2D(0, 0, width, height)
                     mipmapLevel:0
                       withBytes:pixels
                     bytesPerRow:width * 4];

    io.Fonts->SetTexID((ImTextureID)(__bridge void*)g_FontTexture);
    return true;
}

void ImGui_ImplMetal_DestroyFontsTexture() {
    g_FontTexture = nil;
}

bool ImGui_ImplMetal_CreateDeviceObjects(void* device) {
    @autoreleasepool {
        NSError* error = nil;
        NSString* src = [NSString stringWithUTF8String:g_ShaderSource];
        id<MTLLibrary> lib = [g_Device newLibraryWithSource:src options:nil error:&error];
        if (!lib) return false;

        id<MTLFunction> vertFunc = [lib newFunctionWithName:@"vertex_main"];
        id<MTLFunction> fragFunc = [lib newFunctionWithName:@"fragment_main"];

        MTLVertexDescriptor* vtxDesc = [MTLVertexDescriptor vertexDescriptor];
        vtxDesc.attributes[0].format = MTLVertexFormatFloat2;
        vtxDesc.attributes[0].offset = offsetof(ImDrawVert, pos);
        vtxDesc.attributes[0].bufferIndex = 0;

        vtxDesc.attributes[1].format = MTLVertexFormatFloat2;
        vtxDesc.attributes[1].offset = offsetof(ImDrawVert, uv);
        vtxDesc.attributes[1].bufferIndex = 0;

        vtxDesc.attributes[2].format = MTLVertexFormatUChar4;
        vtxDesc.attributes[2].offset = offsetof(ImDrawVert, col);
        vtxDesc.attributes[2].bufferIndex = 0;

        vtxDesc.layouts[0].stride = sizeof(ImDrawVert);
        vtxDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pDesc.vertexFunction = vertFunc;
        pDesc.fragmentFunction = fragFunc;
        pDesc.vertexDescriptor = vtxDesc;
        pDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pDesc.colorAttachments[0].blendingEnabled = YES;
        pDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        g_PipelineState = [g_Device newRenderPipelineStateWithDescriptor:pDesc error:&error];

        MTLDepthStencilDescriptor* dDesc = [[MTLDepthStencilDescriptor alloc] init];
        dDesc.depthCompareFunction = MTLCompareFunctionAlways;
        dDesc.depthWriteEnabled = NO;
        g_DepthStencilState = [g_Device newDepthStencilStateWithDescriptor:dDesc];

        MTLSamplerDescriptor* sDesc = [[MTLSamplerDescriptor alloc] init];
        sDesc.minFilter = MTLSamplerMinMagFilterLinear;
        sDesc.magFilter = MTLSamplerMinMagFilterLinear;
        sDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        g_SamplerState = [g_Device newSamplerStateWithDescriptor:sDesc];

        return true;
    }
}

void ImGui_ImplMetal_DestroyDeviceObjects() {
    g_PipelineState = nil;
    g_DepthStencilState = nil;
    g_SamplerState = nil;
    ImGui_ImplMetal_DestroyFontsTexture();
}

void ImGui_ImplMetal_RenderDrawData(ImDrawData* draw_data, void* command_buffer, void* command_encoder) {
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)command_encoder;
    [enc setRenderPipelineState:g_PipelineState];
    [enc setDepthStencilState:g_DepthStencilState];
    [enc setFragmentSamplerState:g_SamplerState atIndex:0];

    // Setup projection matrix
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    float N = 0.0f;
    float F = 1.0f;

    simd::float4x4 proj = simd::float4x4(
        simd::float4{ 2.0f / (R - L), 0.0f, 0.0f, 0.0f },
        simd::float4{ 0.0f, 2.0f / (T - B), 0.0f, 0.0f },
        simd::float4{ 0.0f, 0.0f, 1.0f / (F - N), 0.0f },
        simd::float4{ (R + L) / (L - R), (T + B) / (B - T), N / (N - F), 1.0f }
    );
    [enc setVertexBytes:&proj length:sizeof(proj) atIndex:1];

    ImVec2 clip_off = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        id<MTLBuffer> vtxBuf = [g_Device newBufferWithBytes:cmd_list->VtxBuffer.Data
                                                     length:cmd_list->VtxBuffer.Size * sizeof(ImDrawVert)
                                                    options:MTLResourceStorageModeShared];

        id<MTLBuffer> idxBuf = [g_Device newBufferWithBytes:cmd_list->IdxBuffer.Data
                                                     length:cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx)
                                                    options:MTLResourceStorageModeShared];

        [enc setVertexBuffer:vtxBuf offset:0 atIndex:0];

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
            } else {
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                MTLScissorRect scissorRect = {
                    (NSUInteger)clip_min.x,
                    (NSUInteger)clip_min.y,
                    (NSUInteger)(clip_max.x - clip_min.x),
                    (NSUInteger)(clip_max.y - clip_min.y)
                };
                [enc setScissorRect:scissorRect];

                id<MTLTexture> tex = (__bridge id<MTLTexture>)(void*)pcmd->GetTexID();
                if (tex) [enc setFragmentTexture:tex atIndex:0];

                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:pcmd->ElemCount
                                 indexType:sizeof(ImDrawIdx) == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                               indexBuffer:idxBuf
                         indexBufferOffset:pcmd->IdxOffset * sizeof(ImDrawIdx)
                             instanceCount:1
                                baseVertex:pcmd->VtxOffset
                              baseInstance:0];
            }
        }
    }
}
