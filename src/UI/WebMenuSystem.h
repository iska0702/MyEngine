#pragma once

#if defined(_WIN32)
#include <windows.h>
#include <wrl.h>
#else
#include "../Platform/PlatformTypes.h"
#endif
#include <string>
#include <memory>
#include <functional>
#include <vector>

class Renderer;
class Window;
class Camera;

namespace EngineUI {

class WebMenuSystem {
public:
    static WebMenuSystem& Get();

    bool Initialize(HWND hwnd, Renderer* renderer, Window* window);
    void Shutdown();

    void Update(float dt, float fps, Renderer* renderer, Window* window);
    void DrawFallbackUI(Renderer* renderer, Window* window, Camera* camera);
    void Resize(int width, int height);

    void ToggleMenu();
    void SetMenuOpen(bool open, Window* window = nullptr);
    void SetOverlayOccluded(bool occluded);
    bool IsMenuOpen() const { return m_isMenuOpen; }
    bool IsInitialized() const { return m_initialized; }
    bool IsWebView2Ready() const { return m_webView2Ready; }

    // Dispatch JSON action from JavaScript to Engine
    void DispatchAction(const std::string& actionJson, Renderer* renderer, Window* window);

    // Send Live Telemetry to WebUI
    void SendTelemetry(float fps, Renderer* renderer);

private:
    WebMenuSystem();
    ~WebMenuSystem();
    WebMenuSystem(const WebMenuSystem&) = delete;
    WebMenuSystem& operator=(const WebMenuSystem&) = delete;

    HWND m_hwnd = nullptr;
    bool m_initialized = false;
    bool m_webView2Ready = false;
    bool m_isMenuOpen = false; // Start in gameplay/editor, toggle with ESC to prevent black occlusion
    float m_telemetryTimer = 0.0f;
    float m_projectSplashRemaining = 0.0f;
    int m_width = 1920;
    int m_height = 1080;

#if defined(_WIN32)
    // WebView2 COM pointers (kept as IUnknown to avoid global header pollution)
    Microsoft::WRL::ComPtr<IUnknown> m_environment;
    Microsoft::WRL::ComPtr<IUnknown> m_controller;
    Microsoft::WRL::ComPtr<IUnknown> m_webView;
#else
    void* m_environment = nullptr;
    void* m_controller = nullptr;
    void* m_webView = nullptr;
#endif

    void CreateWebView2Environment(HWND hwnd);
    std::string LoadInlinedHtmlFromPak();
    std::wstring GetHtmlContentUri();
};

} // namespace EngineUI
