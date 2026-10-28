// dear imgui: Renderer Backend for Metal
// This needs to be used along with a Platform Backend (e.g. OSX)

#pragma once
#include "imgui.h"

#ifdef __OBJC__
@class MTLRenderPassDescriptor;
@protocol MTLDevice, MTLCommandBuffer, MTLRenderCommandEncoder;
#else
typedef void MTLRenderPassDescriptor;
typedef void MTLDevice;
typedef void MTLCommandBuffer;
typedef void MTLRenderCommandEncoder;
#endif

IMGUI_IMPL_API bool ImGui_ImplMetal_Init(void* device);
IMGUI_IMPL_API void ImGui_ImplMetal_Shutdown();
IMGUI_IMPL_API void ImGui_ImplMetal_NewFrame(void* renderPassDescriptor);
IMGUI_IMPL_API void ImGui_ImplMetal_RenderDrawData(ImDrawData* draw_data,
                                                   void* command_buffer,
                                                   void* command_encoder);

// Called by Init/NewFrame/Shutdown
IMGUI_IMPL_API bool ImGui_ImplMetal_CreateFontsTexture(void* device);
IMGUI_IMPL_API void ImGui_ImplMetal_DestroyFontsTexture();
IMGUI_IMPL_API bool ImGui_ImplMetal_CreateDeviceObjects(void* device);
IMGUI_IMPL_API void ImGui_ImplMetal_DestroyDeviceObjects();
