#pragma once

#include <string>
#include <cstdint>

#ifdef __OBJC__
@class NSWindow;
@class NSView;
@class CAMetalLayer;
#else
typedef void NSWindow;
typedef void NSView;
typedef void CAMetalLayer;
#endif

namespace EnginePlatform {

class MacWindow {
public:
    MacWindow(int width, int height, const std::string& title);
    ~MacWindow();

    bool Initialize();
    void Show();
    void* GetMetalLayer() const { return m_metalLayer; }
    void* GetNSWindow() const { return m_nsWindow; }

    void ToggleFullscreen();
    bool IsFullscreen() const { return m_isFullscreen; }
    void SetDisplayMode(int width, int height, const std::string& mode);
    int GetClientWidth() const { return m_width; }
    int GetClientHeight() const { return m_height; }

    bool IsOpen() const { return m_isOpen; }
    void SetOpen(bool open) { m_isOpen = open; }
    void OnViewResize(int width, int height);

    bool IsCursorEnabled() const { return m_cursorEnabled; }
    void SetCursorEnabled(bool enabled);

    bool IsKeyDown(int key) const;
    void GetMouseDelta(int& dx, int& dy);
    void ResetMouseDelta();

    // Pump window and input events (called in main loop)
    bool ProcessEvents();

private:
    int m_width = 1280;
    int m_height = 720;
    std::string m_title;

    void* m_nsWindow = nullptr;
    void* m_nsView = nullptr;
    void* m_metalLayer = nullptr;
    void* m_windowDelegate = nullptr;

    bool m_keys[256] = {};
    int m_lastMouseX = -1;
    int m_lastMouseY = -1;
    int m_mouseDeltaX = 0;
    int m_mouseDeltaY = 0;
    bool m_cursorEnabled = false;
    bool m_isFullscreen = false;
    bool m_isOpen = true;
};

} // namespace EnginePlatform
