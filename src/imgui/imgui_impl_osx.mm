#import "imgui_impl_osx.h"
#import <Cocoa/Cocoa.h>

static NSView* g_View = nil;

bool ImGui_ImplOSX_Init(void* view) {
    g_View = (__bridge NSView*)view;
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_osx";
    return true;
}

void ImGui_ImplOSX_Shutdown() {
    g_View = nil;
}

void ImGui_ImplOSX_NewFrame(void* view) {
    NSView* v = view ? (__bridge NSView*)view : g_View;
    if (!v) return;

    ImGuiIO& io = ImGui::GetIO();

    NSRect bounds = [v bounds];
    NSWindow* window = [v window];
    CGFloat scale = window ? [window backingScaleFactor] : 1.0;

    io.DisplaySize = ImVec2(bounds.size.width, bounds.size.height);
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    if (window) {
        NSPoint mousePos = [window mouseLocationOutsideOfEventStream];
        mousePos = [v convertPoint:mousePos fromView:nil];
        io.AddMousePosEvent(mousePos.x, bounds.size.height - mousePos.y);

        NSUInteger buttons = [NSEvent pressedMouseButtons];
        io.AddMouseButtonEvent(0, (buttons & 1) != 0);
        io.AddMouseButtonEvent(1, (buttons & 2) != 0);
        io.AddMouseButtonEvent(2, (buttons & 4) != 0);
    }
}
