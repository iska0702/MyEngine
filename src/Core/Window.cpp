#include "Window.h"
#if defined(_WIN32) && !defined(ENGINE_RELEASE)
#include <imgui.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

#if defined(_WIN32)
#include "DualSense.h"

Window::Window(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title), m_hwnd(nullptr) {
}

Window::~Window() {
    EngineInput::DualSenseController::Get().TurnOffAll();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

PlatformWindowHandle Window::GetNativeHandle() const {
    return m_hwnd;
}

bool Window::Initialize() {
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    const wchar_t* className = L"IdontknowWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = className;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    RECT windowRect = { 0, 0, m_width, m_height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        className,
        m_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) {
        return false;
    }
    RAWINPUTDEVICE mouse {};
    mouse.usUsagePage = 0x01;
    mouse.usUsage = 0x02;
    mouse.hwndTarget = m_hwnd;
    // Keep legacy messages for UI controls; camera movement uses raw counts.
    m_rawMouseAvailable = RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) != FALSE;

    return true;
}

void Window::Show() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOW);
        SetCursorEnabled(m_cursorEnabled);
        EngineInput::DualSenseController::Get().Activate();
    }
}

void Window::SetCursorEnabled(bool enabled) {
    m_cursorEnabled = enabled;
    m_mouseDeltaX = m_mouseDeltaY = 0;
    m_lastMouseX = m_lastMouseY = -1;
    if (enabled) {
        while (ShowCursor(TRUE) < 0);
        ClipCursor(nullptr);
        ReleaseCapture();
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    } else {
        while (ShowCursor(FALSE) >= 0);
        SetCursor(nullptr);
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        if (GetForegroundWindow() == m_hwnd) {
            RECT screenRect = rect;
            MapWindowPoints(m_hwnd, nullptr, reinterpret_cast<POINT*>(&screenRect), 2);
            ClipCursor(&screenRect);
        }
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(m_hwnd, &center);
        SetCursorPos(center.x, center.y);
    }
}

void Window::ToggleFullscreen() {
    static WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };
    DWORD dwStyle = (DWORD)GetWindowLongPtr(m_hwnd, GWL_STYLE);
    
    if (dwStyle & WS_OVERLAPPEDWINDOW) {
        MONITORINFO mi = { sizeof(mi) };
        g_wpPrev.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(m_hwnd, &g_wpPrev) &&
            GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            SetWindowLongPtr(m_hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(m_hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    } else {
        SetWindowLongPtr(m_hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(m_hwnd, &g_wpPrev);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }

    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);

    RECT rect;
    GetClientRect(m_hwnd, &rect);
    m_width = rect.right - rect.left;
    m_height = rect.bottom - rect.top;
}

void Window::SetDisplayMode(int width, int height, const std::string& mode) {
    if (!m_hwnd) return;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    int screenW = mi.rcMonitor.right - mi.rcMonitor.left;
    int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    if (mode == "fullscreen") {
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOPMOST,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     screenW, screenH,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_width = (width > 0) ? width : screenW;
        m_height = (height > 0) ? height : screenH;
    } else if (mode == "borderless") {
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     screenW, screenH,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_width = screenW;
        m_height = screenH;
    } else { // windowed
        int targetW = (width > 0 && width <= screenW) ? width : 1280;
        int targetH = (height > 0 && height <= screenH) ? height : 720;
        
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        RECT rect = { 0, 0, targetW, targetH };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        int winW = rect.right - rect.left;
        int winH = rect.bottom - rect.top;
        int posX = mi.rcMonitor.left + (screenW - winW) / 2;
        int posY = mi.rcMonitor.top + (screenH - winH) / 2;
        if (posX < mi.rcMonitor.left) posX = mi.rcMonitor.left;
        if (posY < mi.rcMonitor.top) posY = mi.rcMonitor.top;

        SetWindowPos(m_hwnd, HWND_NOTOPMOST,
                     posX, posY, winW, winH,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_width = targetW;
        m_height = targetH;
    }

    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    SetFocus(m_hwnd);
}

bool Window::IsOpen() const {
    return m_hwnd != nullptr;
}

bool Window::IsFullscreen() const {
    DWORD dwStyle = GetWindowLong(m_hwnd, GWL_STYLE);
    return !(dwStyle & WS_OVERLAPPEDWINDOW);
}

int Window::GetClientWidth() const {
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    return rect.right - rect.left;
}

int Window::GetClientHeight() const {
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    return rect.bottom - rect.top;
}

bool Window::IsKeyDown(int key) const {
    if (key >= 0 && key < 256) {
        return m_keys[key];
    }
    return false;
}

void Window::GetMouseDelta(int& dx, int& dy) {
    dx = 0;
    dy = 0;
    if (m_cursorEnabled || GetForegroundWindow() != m_hwnd) {
        m_mouseDeltaX = m_mouseDeltaY = 0;
        return;
    }
    if (m_rawMouseAvailable) {
        dx = m_mouseDeltaX;
        dy = m_mouseDeltaY;
        m_mouseDeltaX = m_mouseDeltaY = 0;
        return;
    }

    POINT p;
    GetCursorPos(&p);
    ScreenToClient(m_hwnd, &p);

    RECT rect;
    GetClientRect(m_hwnd, &rect);
    int centerX = (rect.right - rect.left) / 2;
    int centerY = (rect.bottom - rect.top) / 2;

    dx = p.x - centerX;
    dy = p.y - centerY;

    if (dx != 0 || dy != 0) {
        POINT centerPt = { centerX, centerY };
        ClientToScreen(m_hwnd, &centerPt);
        SetCursorPos(centerPt.x, centerPt.y);
    }
}

void Window::ResetMouseDelta() {
    m_mouseDeltaX = m_mouseDeltaY = 0;
}

bool Window::ProcessEvents() {
    return true;
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Window* window = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->HandleMessage(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_INPUT) {
        RAWINPUT input {};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            input.header.dwType == RIM_TYPEMOUSE && !m_cursorEnabled && GetForegroundWindow() == hwnd) {
            if ((input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                m_mouseDeltaX += input.data.mouse.lLastX;
                m_mouseDeltaY += input.data.mouse.lLastY;
            } else {
                const bool desktop = (input.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
                const int width = GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
                const int height = GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                const int x = MulDiv(input.data.mouse.lLastX, width, 65535);
                const int y = MulDiv(input.data.mouse.lLastY, height, 65535);
                if (m_lastMouseX != -1 && m_lastMouseY != -1) {
                    m_mouseDeltaX += x - m_lastMouseX;
                    m_mouseDeltaY += y - m_lastMouseY;
                }
                m_lastMouseX = x;
                m_lastMouseY = y;
            }
        }
        // Windows requires default processing to release foreground raw-input data.
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    // Track keys BEFORE ImGui so gameplay & UI toggles (like TAB, F11, ~, WASD) are never lost
    if (uMsg == WM_KEYDOWN && wParam < 256) {
        m_keys[wParam] = true;
        if (((lParam >> 16) & 0xFF) == 0x29) m_keys[VK_OEM_3] = true;
    } else if (uMsg == WM_KEYUP && wParam < 256) {
        m_keys[wParam] = false;
        if (((lParam >> 16) & 0xFF) == 0x29) m_keys[VK_OEM_3] = false;
    }

#ifndef ENGINE_RELEASE
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;
#endif

    switch (uMsg) {
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (m_cursorEnabled) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                } else {
                    SetCursor(nullptr);
                    return TRUE;
                }
            }
            break;
        }

        case WM_SYSKEYDOWN: {
            if (wParam == VK_F4) {
                SendMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            bool prevCursorEnabled = m_cursorEnabled;
            m_cursorEnabled = true;
            while (ShowCursor(TRUE) < 0);
            ClipCursor(nullptr);
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));

            int result = MessageBoxW(hwnd, 
                L"Are you sure you want to exit the game?", 
                L"Idontknow - Exit Confirmation", 
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_APPLMODAL | MB_SETFOREGROUND | MB_TOPMOST);

            if (result == IDYES) {
                EngineInput::DualSenseController::Get().TurnOffAll();
                DestroyWindow(hwnd);
                PostQuitMessage(0);
            } else {
                SetCursorEnabled(prevCursorEnabled);
            }
            return 0;
        }

        case WM_DESTROY:
            EngineInput::DualSenseController::Get().TurnOffAll();
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                m_width = LOWORD(lParam);
                m_height = HIWORD(lParam);
                if (!m_cursorEnabled && GetForegroundWindow() == hwnd) SetCursorEnabled(false);
            }
            return 0;

        case WM_ACTIVATE:
            ResetMouseDelta();
            if (LOWORD(wParam) == WA_INACTIVE) {
                memset(m_keys, 0, sizeof(m_keys));
                while (ShowCursor(TRUE) < 0);
                ClipCursor(nullptr);
                ReleaseCapture();
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                EngineInput::DualSenseController::Get().Pause();
            } else {
                SetCursorEnabled(m_cursorEnabled);
                if (!m_cursorEnabled) {
                    while (ShowCursor(FALSE) >= 0);
                    SetCursor(nullptr);
                } else {
                    while (ShowCursor(TRUE) < 0);
                    ClipCursor(nullptr);
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
                
                // Center cursor on activation if cursor is hidden/disabled
                if (!m_cursorEnabled) {
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                    ClientToScreen(hwnd, &center);
                    SetCursorPos(center.x, center.y);
                }
                
                m_lastMouseX = -1;
                m_lastMouseY = -1;
                EngineInput::DualSenseController::Get().Resume();
            }
            return 0;

        case WM_MOUSEMOVE:
            return 0;

        case WM_RBUTTONUP:
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#elif defined(__APPLE__)
#include "Platform/macOS/MacWindow.h"

Window::Window(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {
    std::string utf8Title(title.begin(), title.end());
    m_macWindow = std::make_unique<EnginePlatform::MacWindow>(width, height, utf8Title);
}

Window::~Window() = default;

PlatformWindowHandle Window::GetNativeHandle() const {
    if (m_macWindow) return m_macWindow->GetMetalLayer();
    return nullptr;
}

bool Window::Initialize() {
    if (m_macWindow) {
        return m_macWindow->Initialize();
    }
    return false;
}

void Window::Show() {
    if (m_macWindow) {
        m_macWindow->Show();
    }
}

void Window::SetCursorEnabled(bool enabled) {
    m_cursorEnabled = enabled;
    if (m_macWindow) {
        m_macWindow->SetCursorEnabled(enabled);
    }
}

void Window::ToggleFullscreen() {
    if (m_macWindow) {
        m_macWindow->ToggleFullscreen();
    }
}

void Window::SetDisplayMode(int width, int height, const std::string& mode) {
    if (m_macWindow) {
        m_macWindow->SetDisplayMode(width, height, mode);
        m_width = m_macWindow->GetClientWidth();
        m_height = m_macWindow->GetClientHeight();
    }
}

bool Window::IsOpen() const {
    if (m_macWindow) return m_macWindow->IsOpen();
    return false;
}

bool Window::IsFullscreen() const {
    if (m_macWindow) return m_macWindow->IsFullscreen();
    return false;
}

int Window::GetClientWidth() const {
    if (m_macWindow) return m_macWindow->GetClientWidth();
    return m_width;
}

int Window::GetClientHeight() const {
    if (m_macWindow) return m_macWindow->GetClientHeight();
    return m_height;
}

bool Window::IsKeyDown(int key) const {
    if (m_macWindow) return m_macWindow->IsKeyDown(key);
    return false;
}

void Window::GetMouseDelta(int& dx, int& dy) {
    if (m_macWindow) {
        m_macWindow->GetMouseDelta(dx, dy);
    } else {
        dx = 0;
        dy = 0;
    }
}

void Window::ResetMouseDelta() {
    if (m_macWindow) {
        m_macWindow->ResetMouseDelta();
    }
}

bool Window::ProcessEvents() {
    if (m_macWindow) {
        return m_macWindow->ProcessEvents();
    }
    return true;
}

#elif defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)
#include "Platform/PS4/PS4Window.h"

Window::Window(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {
    m_ps4Window = std::make_unique<EnginePlatform::PS4Window>(width, height, title);
}

Window::~Window() = default;

PlatformWindowHandle Window::GetNativeHandle() const {
    if (m_ps4Window) return m_ps4Window->GetNativeHandle();
    return nullptr;
}

bool Window::Initialize() {
    if (m_ps4Window) {
        return m_ps4Window->Initialize();
    }
    return false;
}

void Window::Show() {
    if (m_ps4Window) {
        m_ps4Window->Show();
    }
}

void Window::SetCursorEnabled(bool enabled) {
    m_cursorEnabled = enabled;
    if (m_ps4Window) {
        m_ps4Window->SetCursorEnabled(enabled);
    }
}

void Window::ToggleFullscreen() {
    if (m_ps4Window) {
        m_ps4Window->ToggleFullscreen();
    }
}

void Window::SetDisplayMode(int width, int height, const std::string& mode) {
    if (m_ps4Window) {
        m_ps4Window->SetDisplayMode(width, height, mode);
        m_width = m_ps4Window->GetClientWidth();
        m_height = m_ps4Window->GetClientHeight();
    }
}

bool Window::IsOpen() const {
    if (m_ps4Window) return m_ps4Window->IsOpen();
    return false;
}

bool Window::IsFullscreen() const {
    if (m_ps4Window) return m_ps4Window->IsFullscreen();
    return true;
}

int Window::GetClientWidth() const {
    if (m_ps4Window) return m_ps4Window->GetClientWidth();
    return m_width;
}

int Window::GetClientHeight() const {
    if (m_ps4Window) return m_ps4Window->GetClientHeight();
    return m_height;
}

bool Window::IsKeyDown(int key) const {
    if (m_ps4Window) return m_ps4Window->IsKeyDown(key);
    return false;
}

void Window::GetMouseDelta(int& dx, int& dy) {
    if (m_ps4Window) {
        m_ps4Window->GetMouseDelta(dx, dy);
    } else {
        dx = 0;
        dy = 0;
    }
}

void Window::ResetMouseDelta() {
    if (m_ps4Window) {
        m_ps4Window->ResetMouseDelta();
    }
}

bool Window::ProcessEvents() {
    if (m_ps4Window) {
        return m_ps4Window->ProcessEvents();
    }
    return true;
}

#else

Window::Window(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {}
Window::~Window() = default;
PlatformWindowHandle Window::GetNativeHandle() const { return nullptr; }
bool Window::Initialize() { return true; }
void Window::Show() {}
void Window::SetCursorEnabled(bool) {}
void Window::ToggleFullscreen() {}
void Window::SetDisplayMode(int, int, const std::string&) {}
bool Window::IsOpen() const { return true; }
bool Window::IsFullscreen() const { return false; }
int Window::GetClientWidth() const { return m_width; }
int Window::GetClientHeight() const { return m_height; }
bool Window::IsKeyDown(int) const { return false; }
void Window::GetMouseDelta(int& dx, int& dy) { dx = 0; dy = 0; }
void Window::ResetMouseDelta() {}
bool Window::ProcessEvents() { return true; }

#endif
