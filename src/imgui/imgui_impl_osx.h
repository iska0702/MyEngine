// dear imgui: Platform Backend for macOS (Cocoa/AppKit)

#pragma once
#include "imgui.h"

IMGUI_IMPL_API bool ImGui_ImplOSX_Init(void* view);
IMGUI_IMPL_API void ImGui_ImplOSX_Shutdown();
IMGUI_IMPL_API void ImGui_ImplOSX_NewFrame(void* view);
