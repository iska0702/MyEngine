#pragma once

#include "Platform/PlatformTypes.h"
#include <string>
#include <memory>

#if defined(__APPLE__)
namespace EnginePlatform { class MacWindow; }
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
namespace EnginePlatform { class PS4Window; }
#endif

class Window {
public:
    Window(int width, int height, const std::wstring& title);
    ~Window();

    bool Initialize();
    void Show();

    PlatformWindowHandle GetNativeHandle() const;

#if defined(_WIN32)
    HWND GetHWND() const { return m_hwnd; }
#else
    void* GetHWND() const { return GetNativeHandle(); }
#endif

    bool IsOpen() const;

    void ToggleFullscreen();
    bool IsFullscreen() const;
    void SetDisplayMode(int width, int height, const std::string& mode);
    int GetClientWidth() const;
    int GetClientHeight() const;

    bool IsCursorEnabled() const { return m_cursorEnabled; }
    void SetCursorEnabled(bool enabled);

    bool IsKeyDown(int key) const;
    void GetMouseDelta(int& dx, int& dy);
    void ResetMouseDelta();

    // Process platform events (called once per frame)
    bool ProcessEvents();

private:
#if defined(_WIN32)
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    HWND m_hwnd = nullptr;
    bool m_rawMouseAvailable = false;
#elif defined(__APPLE__)
    std::unique_ptr<EnginePlatform::MacWindow> m_macWindow;
#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
    std::unique_ptr<EnginePlatform::PS4Window> m_ps4Window;
public:
    EnginePlatform::PS4Window* GetPS4Window() const { return m_ps4Window.get(); }
private:
#endif

    int m_width;
    int m_height;
    std::wstring m_title;

    bool m_keys[256] = {};
    int m_lastMouseX = -1;
    int m_lastMouseY = -1;
    int m_mouseDeltaX = 0;
    int m_mouseDeltaY = 0;
    bool m_cursorEnabled = false;
};
