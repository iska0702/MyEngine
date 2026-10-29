#include "WebMenuSystem.h"
#include "Core/Project.h"
#include "Renderer/Renderer.h"
#include "Core/Window.h"
#include "Renderer/Camera.h"
#include "Physics/ClothSystem.h"
#include "Physics/HairSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Rain/RainSystem.h"
#include "Networking/MultiplayerManager.h"
#include "Audio/DualSenseAudio.h"
#include "Steam/SteamSystem.h"
#include "Epic/EpicSystem.h"
#include "Level/LevelSystem.h"

#if defined(_WIN32)
#include <wrl/event.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <shlobj.h>
#include "../../ThirdParty/Microsoft/WebView2-1.0.4314.35/build/native/include/WebView2.h"
using namespace Microsoft::WRL;
static EventRegistrationToken s_webMessageToken = {};
#endif

#include <filesystem>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#if !defined(ENGINE_RELEASE)
#include <imgui.h>
#endif

namespace EngineUI {

static Renderer* s_cachedRenderer = nullptr;
static Window* s_cachedWindow = nullptr;

WebMenuSystem& WebMenuSystem::Get() {
    static WebMenuSystem s_instance;
    return s_instance;
}

WebMenuSystem::WebMenuSystem() {
}

WebMenuSystem::~WebMenuSystem() {
    Shutdown();
}

static std::string ReadFileContent(const std::wstring& pakPath, const std::vector<std::string>& localPaths) {
    // 1. Try PAK
    auto bytes = Renderer::LoadFromPak(pakPath.c_str());
    if (!bytes.empty()) {
        return std::string(bytes.begin(), bytes.end());
    }

    // 2. Try local files
    for (const auto& p : localPaths) {
        if (std::filesystem::exists(p)) {
            std::ifstream f(p, std::ios::binary);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                return ss.str();
            }
        }
    }
    return "";
}

std::string WebMenuSystem::LoadInlinedHtmlFromPak() {
    // 1. Try loading main_menu.html or index.html
    std::string htmlStr = ReadFileContent(L"Content/UI/main_menu.html", {
        "Content/UI/main_menu.html",
        "UI/main_menu.html",
        "../src/Content/UI/main_menu.html",
        "d:/MyEngine/src/Content/UI/main_menu.html",
        "Content/UI/index.html",
        "../src/Content/UI/index.html"
    });

    if (htmlStr.empty()) {
        htmlStr = ReadFileContent(L"Content/UI/tab_menu.html", {
            "Content/UI/tab_menu.html",
            "../src/Content/UI/tab_menu.html",
            "d:/MyEngine/src/Content/UI/tab_menu.html"
        });
    }

    if (htmlStr.empty()) return "";

    // 2. Load style.css
    std::string cssStr = ReadFileContent(L"Content/UI/style.css", {
        "Content/UI/style.css",
        "UI/style.css",
        "../src/Content/UI/style.css",
        "d:/MyEngine/src/Content/UI/style.css"
    });

    // 3. Load app.js
    std::string appJsStr = ReadFileContent(L"Content/UI/app.js", {
        "Content/UI/app.js",
        "UI/app.js",
        "../src/Content/UI/app.js",
        "d:/MyEngine/src/Content/UI/app.js"
    });

    // 4. Load main_menu.js
    std::string mainJsStr = ReadFileContent(L"Content/UI/main_menu.js", {
        "Content/UI/main_menu.js",
        "UI/main_menu.js",
        "../src/Content/UI/main_menu.js",
        "d:/MyEngine/src/Content/UI/main_menu.js"
    });

    // 5. Load tab_menu.js
    std::string tabJsStr = ReadFileContent(L"Content/UI/tab_menu.js", {
        "Content/UI/tab_menu.js",
        "UI/tab_menu.js",
        "../src/Content/UI/tab_menu.js",
        "d:/MyEngine/src/Content/UI/tab_menu.js"
    });

    // Inline CSS into <style>
    if (!cssStr.empty()) {
        size_t linkPos = htmlStr.find("<link rel=\"stylesheet\" href=\"style.css\">");
        if (linkPos != std::string::npos) {
            htmlStr.replace(linkPos, 40, "<style>\n" + cssStr + "\n</style>");
        }
    }

    // Inline app.js into <script>
    if (!appJsStr.empty()) {
        size_t scriptPos = htmlStr.find("<script src=\"app.js\"></script>");
        if (scriptPos != std::string::npos) {
            htmlStr.replace(scriptPos, 30, "<script>\n" + appJsStr + "\n</script>");
        }
    }

    // Inline main_menu.js into <script>
    if (!mainJsStr.empty()) {
        size_t scriptPos = htmlStr.find("<script src=\"main_menu.js\"></script>");
        if (scriptPos != std::string::npos) {
            htmlStr.replace(scriptPos, 36, "<script>\n" + mainJsStr + "\n</script>");
        }
    }

    // Inline tab_menu.js into <script>
    if (!tabJsStr.empty()) {
        size_t scriptPos = htmlStr.find("<script src=\"tab_menu.js\"></script>");
        if (scriptPos != std::string::npos) {
            htmlStr.replace(scriptPos, 35, "<script>\n" + tabJsStr + "\n</script>");
        }
    }

    return htmlStr;
}

#if defined(_WIN32)
std::wstring WebMenuSystem::GetHtmlContentUri() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath);

    std::wstring uiPath = std::wstring(exePath) + L"\\Content\\UI\\main_menu.html";
    if (GetFileAttributesW(uiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        uiPath = std::wstring(exePath) + L"\\Content\\UI\\tab_menu.html";
    }
    if (GetFileAttributesW(uiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        uiPath = std::wstring(exePath) + L"\\Content\\UI\\index.html";
    }
    if (GetFileAttributesW(uiPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        uiPath = L"d:\\MyEngine\\src\\Content\\UI\\main_menu.html";
    }
    return uiPath;
}

void WebMenuSystem::CreateWebView2Environment(HWND hwnd) {
    LOG("WebMenuSystem: Initializing Microsoft WebView2 Runtime...");

    wchar_t localAppData[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    std::wstring userDataFolder = std::wstring(localAppData) + L"\\Idontknow_WebView2_UserData";

    try {
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userDataFolder.c_str(),
            nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result) || !env) {
                        LOG("Failed to create WebView2 Environment. HRESULT: 0x" << std::hex << result << std::dec);
                        return result;
                    }
                    m_environment = env;

                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this, hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(result) || !controller) {
                                    LOG("Failed to create WebView2 Controller. HRESULT: 0x" << std::hex << result << std::dec);
                                    return result;
                                }
                                m_controller = controller;

                                ComPtr<ICoreWebView2> webView;
                                controller->get_CoreWebView2(&webView);
                                m_webView = webView;

                                // 1. Configure Transparent Background
                                ComPtr<ICoreWebView2Controller2> controller2;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                    COREWEBVIEW2_COLOR transparentColor = { 0, 0, 0, 0 };
                                    controller2->put_DefaultBackgroundColor(transparentColor);
                                }

                                // 2. Set Bounds
                                RECT bounds;
                                GetClientRect(hwnd, &bounds);
                                controller->put_Bounds(bounds);
                                controller->put_IsVisible(m_isMenuOpen ? TRUE : FALSE);
                                if (m_isMenuOpen) {
                                    controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                                    if (s_cachedWindow) s_cachedWindow->SetCursorEnabled(true);
                                }

                                // 3. Disable built-in accelerator keys (F5 refresh, ESC, etc.)
                                ComPtr<ICoreWebView2Settings> settings;
                                if (SUCCEEDED(webView->get_Settings(&settings)) && settings) {
                                    settings->put_IsScriptEnabled(TRUE);
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_AreDevToolsEnabled(FALSE);
                                    settings->put_IsZoomControlEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                }

                                // 4. Accelerator keys (Alt+F4 instant quit forwarding)
                                EventRegistrationToken accelToken;
                                controller->add_AcceleratorKeyPressed(
                                    Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                        [this, hwnd](ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                            COREWEBVIEW2_KEY_EVENT_KIND kind;
                                            args->get_KeyEventKind(&kind);
                                            if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                                UINT key = 0;
                                                args->get_VirtualKey(&key);
                                                // Alt + F4 -> Quit Game
                                                if (key == VK_F4 && ((GetKeyState(VK_MENU) & 0x8000) != 0)) {
                                                    args->put_Handled(TRUE);
                                                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                                                    return S_OK;
                                                }

                                                // Shift + Tab -> Steam Overlay
                                                if (key == VK_TAB && ((GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
                                                    args->put_Handled(TRUE);
                                                    EngineSteam::SteamSystem::Get().OpenOverlay();
                                                    return S_OK;
                                                }

                                                // Shift + F3 -> Epic Overlay
                                                if (key == VK_F3 && ((GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
                                                    args->put_Handled(TRUE);
                                                    EngineEpic::EpicSystem::Get().OpenOverlay("Friends");
                                                    return S_OK;
                                                }
                                            }
                                            return S_OK;
                                        }
                                    ).Get(),
                                    &accelToken
                                );

                                // 5. Communication Bridge
                                webView->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR messageRaw = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&messageRaw)) && messageRaw) {
                                                std::wstring wideMsg = messageRaw;
                                                CoTaskMemFree(messageRaw);
                                                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), (int)wideMsg.length(), NULL, 0, NULL, NULL);
                                                std::string strMsg(sizeNeeded, 0);
                                                WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), (int)wideMsg.length(), &strMsg[0], sizeNeeded, NULL, NULL);
                                                this->DispatchAction(strMsg, s_cachedRenderer, s_cachedWindow);
                                            } else if (SUCCEEDED(args->get_WebMessageAsJson(&messageRaw)) && messageRaw) {
                                                std::wstring wideMsg = messageRaw;
                                                CoTaskMemFree(messageRaw);
                                                int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), (int)wideMsg.length(), NULL, 0, NULL, NULL);
                                                std::string strMsg(sizeNeeded, 0);
                                                WideCharToMultiByte(CP_UTF8, 0, wideMsg.c_str(), (int)wideMsg.length(), &strMsg[0], sizeNeeded, NULL, NULL);
                                                this->DispatchAction(strMsg, s_cachedRenderer, s_cachedWindow);
                                            }
                                            return S_OK;
                                        }
                                    ).Get(),
                                    &s_webMessageToken
                                );

                                bool projectPage=false;
                                if(EngineProject::active){
                                    std::string page=EngineProject::current.mainMenu;
#ifdef ENGINE_RELEASE
                                    if(!EngineProject::current.splash.empty()&&EngineProject::current.splashSeconds>0){
                                        page=EngineProject::current.splash;m_projectSplashRemaining=EngineProject::current.splashSeconds;
                                    }
#endif
                                    if(!page.empty()){
                                        auto file=EngineProject::current.Resolve(page);
                                        if(std::filesystem::exists(file)){
                                            wchar_t uri[32768]{};DWORD count=32768;
                                            if(SUCCEEDED(UrlCreateFromPathW(file.c_str(),uri,&count,0))){webView->Navigate(uri);projectPage=true;}
                                        }
                                    }
                                }
                                if(!projectPage){
                                // 5. Load UI Content directly from PAK files (inlined memory string)
                                std::string inlinedHtml = LoadInlinedHtmlFromPak();
                                if (!inlinedHtml.empty()) {
                                    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, inlinedHtml.c_str(), (int)inlinedHtml.length(), NULL, 0);
                                    std::wstring wideHtml(sizeNeeded, 0);
                                    MultiByteToWideChar(CP_UTF8, 0, inlinedHtml.c_str(), (int)inlinedHtml.length(), &wideHtml[0], sizeNeeded);
                                    webView->NavigateToString(wideHtml.c_str());
                                    LOG("WebView2 UI successfully loaded from encrypted .PAK archive!");
                                } else {
                                    std::wstring htmlUri = GetHtmlContentUri();
                                    webView->Navigate(htmlUri.c_str());
                                }

                                }
                                m_webView2Ready = true;
                                m_initialized = true;
                                return S_OK;
                            }
                        ).Get()
                    );
                    return S_OK;
                }
            ).Get()
        );
    } catch (...) {
        LOG("WebMenuSystem::CreateWebView2Environment caught exception. Fallback active.");
    }
}
#else
std::wstring WebMenuSystem::GetHtmlContentUri() {
    return L"";
}
void WebMenuSystem::CreateWebView2Environment(HWND) {
    m_initialized = true;
}
#endif

bool WebMenuSystem::Initialize(HWND hwnd, Renderer* renderer, Window* window) {
    LOG("WebMenuSystem::Initialize starting...");
    m_hwnd = hwnd;
    s_cachedRenderer = renderer;
    s_cachedWindow = window;

    if (window) {
        m_width = window->GetClientWidth();
        m_height = window->GetClientHeight();
    }

#ifdef ENGINE_RELEASE
    m_isMenuOpen = !EngineProject::active || EngineProject::current.startWithMenu || (!EngineProject::current.splash.empty() && EngineProject::current.splashSeconds>0);
    if (window) {
        window->SetCursorEnabled(m_isMenuOpen);
    }
#else
    m_isMenuOpen = false; // In Editor build, start directly in 3D scene
#endif
    CreateWebView2Environment(hwnd);
    return true;
}

void WebMenuSystem::Shutdown() {
#if defined(_WIN32)
    if (m_controller) {
        ComPtr<ICoreWebView2Controller> ctrl;
        if (SUCCEEDED(m_controller.As(&ctrl))) {
            ctrl->Close();
        }
        m_controller.Reset();
    }
    m_webView.Reset();
    m_environment.Reset();
#endif
    m_initialized = false;
    m_webView2Ready = false;
}

void WebMenuSystem::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    m_width = width;
    m_height = height;

#if defined(_WIN32)
    if (m_controller) {
        ComPtr<ICoreWebView2Controller> ctrl;
        if (SUCCEEDED(m_controller.As(&ctrl))) {
            RECT bounds = { 0, 0, width, height };
            ctrl->put_Bounds(bounds);
        }
    }
#endif
}

void WebMenuSystem::ToggleMenu() {
    SetMenuOpen(!m_isMenuOpen, s_cachedWindow);
}

void WebMenuSystem::SetMenuOpen(bool open, Window* window) {
    m_isMenuOpen = open;
#if defined(_WIN32)
    if (m_controller) {
        ComPtr<ICoreWebView2Controller> ctrl;
        if (SUCCEEDED(m_controller.As(&ctrl))) {
            ctrl->put_IsVisible(open ? TRUE : FALSE);
            if (open) {
                ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            }
        }
    }
#endif

    if (window) {
        window->SetCursorEnabled(open);
    }
}

void WebMenuSystem::SetOverlayOccluded(bool occluded) {
#if defined(_WIN32)
    if (m_controller) {
        ComPtr<ICoreWebView2Controller> ctrl;
        if (SUCCEEDED(m_controller.As(&ctrl))) {
            if (occluded) {
                ctrl->put_IsVisible(FALSE);
            } else {
                ctrl->put_IsVisible(m_isMenuOpen ? TRUE : FALSE);
                if (m_isMenuOpen) {
                    ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                }
            }
        }
    }
#endif
}

static std::vector<std::string> GetSupportedResolutionsJson() {
    std::vector<std::string> resStrings;
#if defined(_WIN32)
    std::vector<std::pair<int, int>> uniqueRes;
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(NULL, i, &dm); ++i) {
        if (dm.dmBitsPerPel >= 24 && dm.dmPelsWidth >= 1024 && dm.dmPelsHeight >= 720) {
            bool exists = false;
            for (const auto& r : uniqueRes) {
                if (r.first == (int)dm.dmPelsWidth && r.second == (int)dm.dmPelsHeight) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                uniqueRes.push_back({ (int)dm.dmPelsWidth, (int)dm.dmPelsHeight });
            }
        }
    }
    std::sort(uniqueRes.begin(), uniqueRes.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second > b.second;
    });

    for (const auto& r : uniqueRes) {
        resStrings.push_back(std::to_string(r.first) + "x" + std::to_string(r.second));
    }
#endif
    if (resStrings.empty()) {
        resStrings = { "2560x1440", "1920x1080", "1600x900", "1280x720" };
    }
    return resStrings;
}

void WebMenuSystem::SendTelemetry(float fps, Renderer* renderer) {
#if defined(_WIN32)
    if (!m_webView || !m_isMenuOpen) return;

    ComPtr<ICoreWebView2> webView;
    if (FAILED(m_webView.As(&webView))) return;

    size_t clothCount = EnginePhysics::ClothSystem::Get().GetCloths().size();
    size_t hairCount = EnginePhysics::HairSystem::Get().GetHairObjects().size();
    size_t physxCount = 0;

    bool dlssSupported = renderer ? renderer->IsDLSSSupported() : false;
    bool dlssRRSupported = renderer ? renderer->IsDLSSRayReconstructionSupported() : false;
    bool dlssRRActive = renderer ? renderer->IsDLSSRayReconstructionActive() : false;
    bool dxrSupported = (renderer && renderer->m_dxrDevice != nullptr);
    int currentWidth = s_cachedWindow ? s_cachedWindow->GetClientWidth() : 1920;
    int currentHeight = s_cachedWindow ? s_cachedWindow->GetClientHeight() : 1080;
    bool isFullscreen = s_cachedWindow ? s_cachedWindow->IsFullscreen() : false;
    bool vsync = renderer ? renderer->IsVSyncEnabled() : true;
    float currentFov = renderer ? renderer->GetFOV() : 90.0f;
    std::string currentUpscaler = (renderer && renderer->GetUpscaleTechnique() == UpscaleTechnique::DLSS) ? "dlss" : ((renderer && renderer->GetUpscaleTechnique() == UpscaleTechnique::XeSS) ? "xess" : ((renderer && renderer->GetUpscaleTechnique() == UpscaleTechnique::FSR) ? "fsr" : "off"));
    std::string currentRt = (renderer && renderer->IsPathTracingEnabled()) ? "path_tracing" : ((renderer && (renderer->m_rtShadowsEnabled || renderer->m_rtReflectionsEnabled)) ? "ray_tracing" : "off");

    std::string currentRr = "off";
    if (renderer && renderer->IsDLSSRayReconstructionActive()) {
        currentRr = (renderer->GetDLSSRayReconstructionVersion() == Renderer::DLSSRRVersion::Version1_0) ? "v1" : "v2";
    }

    auto suppRes = GetSupportedResolutionsJson();
    std::string suppResJson = "[";
    for (size_t i = 0; i < suppRes.size(); ++i) {
        suppResJson += "'" + suppRes[i] + "'";
        if (i + 1 < suppRes.size()) suppResJson += ",";
    }
    suppResJson += "]";

    std::stringstream ss;
    ss << "if (window.EngineBridge && window.EngineBridge.receive) { "
       << "  window.EngineBridge.receive('updateTelemetry', {"
       << "    fps: " << (int)fps << ","
       << "    clothCount: " << clothCount << ","
       << "    hairCount: " << hairCount << ","
       << "    physxCount: " << physxCount << ","
       << "    pathTracing: " << (renderer && renderer->IsPathTracingEnabled() ? "true" : "false") << ","
       << "    windSpeed: " << EnginePhysics::ClothSystem::Get().GetWindSpeed() << ","
       << "    capabilities: {"
       << "      dlssSupported: " << (dlssSupported ? "true" : "false") << ","
       << "      dlssRRSupported: " << (dlssRRSupported ? "true" : "false") << ","
       << "      fsrSupported: true,"
       << "      dxrSupported: " << (dxrSupported ? "true" : "false") << ","
       << "      supportedResolutions: " << suppResJson
       << "    },"
       << "    currentSettings: {"
       << "      resolution: '" << currentWidth << "x" << currentHeight << "',"
       << "      mode: '" << (isFullscreen ? "fullscreen" : "windowed") << "',"
       << "      vsync: " << (vsync ? "true" : "false") << ","
       << "      fov: " << (int)currentFov << ","
       << "      upscaler: '" << currentUpscaler << "',"
       << "      rayReconstruction: '" << currentRr << "',"
       << "      raytracingMode: '" << currentRt << "',"
       << "      volumetricFog: " << (renderer && renderer->IsVolumetricFogEnabled() ? "true" : "false")
       << "    }"
       << "  });"
       << "}";

    std::string js = ss.str();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, js.c_str(), (int)js.length(), NULL, 0);
    std::wstring wideJs(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, js.c_str(), (int)js.length(), &wideJs[0], sizeNeeded);

    webView->ExecuteScript(wideJs.c_str(), nullptr);
#else
    (void)fps;
    (void)renderer;
#endif
}

void WebMenuSystem::Update(float dt, float fps, Renderer* renderer, Window* window) {
    s_cachedRenderer = renderer;
    s_cachedWindow = window;

    if (!m_initialized || !m_webView2Ready) return;

#if defined(_WIN32)
    if(m_projectSplashRemaining>0){
        m_projectSplashRemaining-=dt;
        if(m_projectSplashRemaining<=0){
            ComPtr<ICoreWebView2> web;if(SUCCEEDED(m_webView.As(&web))&&!EngineProject::current.mainMenu.empty()){
                auto page=EngineProject::current.Resolve(EngineProject::current.mainMenu);wchar_t uri[32768]{};DWORD size=32768;
                if(SUCCEEDED(UrlCreateFromPathW(page.c_str(),uri,&size,0)))web->Navigate(uri);
            }
            SetMenuOpen(EngineProject::current.startWithMenu,window);
        }
    }
#endif
    m_telemetryTimer += dt;
    if (m_telemetryTimer >= 0.2f) { // 5 Hz telemetry sync
        m_telemetryTimer = 0.0f;
        SendTelemetry(fps, renderer);
    }
}

void WebMenuSystem::DrawFallbackUI(Renderer* renderer, Window* window, Camera* camera) {
#if defined(MYENGINE_EDITOR)
    if (!m_isMenuOpen) return;

    ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480, 700), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(" In-Game Menu (DirectX 12 / Dear ImGui Fallback)", &m_isMenuOpen, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::Button(" Resume Simulation", ImVec2(200, 35))) {
            SetMenuOpen(false, window);
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader(" Cloth Physics Presets")) {
            if (ImGui::Button("Wind Flag", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_CLOTH\",\"preset\":\"Flag\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Curtain", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_CLOTH\",\"preset\":\"Curtain\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Tablecloth", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_CLOTH\",\"preset\":\"Tablecloth\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Cape", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_CLOTH\",\"preset\":\"Cape\"}", renderer, window);
        }

        if (ImGui::CollapsingHeader(" Hair Physics Presets")) {
            if (ImGui::Button("Long Hair", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_HAIR\",\"preset\":\"LongFlowing\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Ponytail", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_HAIR\",\"preset\":\"Ponytail\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Bob Cut", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_HAIR\",\"preset\":\"BobCut\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Curly", ImVec2(130, 30))) DispatchAction("{\"action\":\"SPAWN_HAIR\",\"preset\":\"CurlyWavy\"}", renderer, window);
        }

        if (ImGui::CollapsingHeader(" Global Wind & PhysX")) {
            float curWind = EnginePhysics::ClothSystem::Get().GetWindSpeed();
            if (ImGui::SliderFloat("Wind Speed", &curWind, 0.0f, 30.0f)) {
                EnginePhysics::ClothSystem::Get().SetWindSpeed(curWind);
            }
            if (ImGui::Button("Spawn Tower")) DispatchAction("{\"action\":\"SPAWN_TOWER\"}", renderer, window);
            ImGui::SameLine();
            if (ImGui::Button("Explosion")) DispatchAction("{\"action\":\"EXPLOSION\"}", renderer, window);
        }

        if (ImGui::CollapsingHeader(" Level / Map")) {
            auto levels = EngineLevel::LevelSystem::Get().GetAvailableLevels();
            for (const auto& lvl : levels) {
                std::string btnLabel = "Load " + lvl;
                if (ImGui::Button(btnLabel.c_str(), ImVec2(160, 25))) {
                    EngineLevel::LevelSystem::Get().RequestLoadLevel(lvl);
                    SetMenuOpen(false, window);
                }
            }
            if (ImGui::Button("Cycle Map (F7)", ImVec2(160, 25))) {
                EngineLevel::LevelSystem::Get().CycleToNextLevel(renderer, camera);
            }
        }

        ImGui::Separator();
        if (ImGui::Button(" Reset Scene", ImVec2(160, 30))) DispatchAction("{\"action\":\"RESET_SCENE\"}", renderer, window);
        ImGui::SameLine();
        if (ImGui::Button(" Exit Game", ImVec2(160, 30))) DispatchAction("{\"action\":\"EXIT_GAME\"}", renderer, window);

        ImGui::End();
    }
#endif
}

// -----------------------------------------------------------------------------------------
// Action Dispatcher: JavaScript -> C++ Engine
// -----------------------------------------------------------------------------------------

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": \"";
        pos = json.find(search);
    }
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos != std::string::npos) {
            pos += search.length();
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\"')) pos++;
            size_t endPos = json.find_first_of("\",}\n", pos);
            if (endPos != std::string::npos) {
                return json.substr(pos, endPos - pos);
            }
        }
    }
    if (pos != std::string::npos) {
        pos += search.length();
        size_t endPos = json.find("\"", pos);
        if (endPos != std::string::npos) {
            return json.substr(pos, endPos - pos);
        }
    }
    return "";
}

static float ExtractJsonFloat(const std::string& json, const std::string& key, float defaultVal = 0.0f) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\"')) pos++;
        size_t endPos = json.find_first_of("\",}\n", pos);
        if (endPos != std::string::npos) {
            try {
                return std::stof(json.substr(pos, endPos - pos));
            } catch (...) {}
        }
    }
    return defaultVal;
}

static int ExtractJsonInt(const std::string& json, const std::string& key, int defaultVal = 0) {
    return (int)ExtractJsonFloat(json, key, (float)defaultVal);
}

static bool ExtractJsonBool(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos != std::string::npos) {
        pos += search.length();
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (json.substr(pos, 4) == "true") return true;
        if (json.substr(pos, 5) == "false") return false;
    }
    return defaultVal;
}

void WebMenuSystem::DispatchAction(const std::string& actionJson, Renderer* renderer, Window* window) {
    std::string action = ExtractJsonString(actionJson, "action");
    if (action.empty()) {
        action = ExtractJsonString(actionJson, "event");
    }
    if (action.empty()) return;

    LOG("WebMenuSystem::DispatchAction: " + action);

    Camera* camera = renderer ? renderer->GetCamera() : nullptr;
    DirectX::XMFLOAT3 camPos = camera ? camera->GetPosition() : DirectX::XMFLOAT3(0, 2.5f, 3.0f);
    DirectX::XMFLOAT3 camFwd = camera ? camera->GetForwardVector() : DirectX::XMFLOAT3(0, 0, -1);

    DirectX::XMFLOAT3 spawnTarget = {
        camPos.x + camFwd.x * 4.0f,
        (std::max)(camPos.y + camFwd.y * 4.0f, 1.5f),
        camPos.z + camFwd.z * 4.0f
    };

    // 1. Lifecycle
    if (action == "RESUME_GAME" || action == "continue_game" || action == "close_tab_menu") {
        SetMenuOpen(false, window);
    } else if (action == "open_steam_overlay") {
        EngineSteam::SteamSystem::Get().OpenOverlay();
    } else if (action == "open_epic_overlay") {
        EngineEpic::EpicSystem::Get().OpenOverlay("Friends");
    } else if (action == "RESET_SCENE" || action == "new_game") {
        EnginePhysics::ClothSystem::Get().ResetAllCloths();
        EnginePhysics::HairSystem::Get().ResetAll();
        EnginePhysics::PhysicsSystem::Get().ResetSimulation(renderer);
    } else if (action == "REQUEST_EXIT") {
#if defined(_WIN32)
        if (window && window->GetHWND()) {
            PostMessage(window->GetHWND(), WM_CLOSE, 0, 0);
        } else {
            PostQuitMessage(0);
        }
#else
        exit(0);
#endif
    } else if (action == "EXIT_GAME" || action == "FORCE_EXIT_GAME") {
#if defined(_WIN32)
        PostQuitMessage(0);
#else
        exit(0);
#endif
    }

    // 2. Fast Travel
    else if (action == "fast_travel") {
        float tx = ExtractJsonFloat(actionJson, "x", 0.0f);
        float ty = ExtractJsonFloat(actionJson, "y", 0.0f);
        if (camera) {
            // Map 2D to 3D world space
            DirectX::XMFLOAT3 newPos = { (tx - 500.0f) * 0.2f, 2.5f, (ty - 300.0f) * 0.2f };
            camera->SetPosition(newPos.x, newPos.y, newPos.z);
        }
    }

    // 3. Settings Application
    else if (action == "save_settings" || action == "SET_SETTINGS") {
        // 1. Display Resolution & Mode
        std::string resStr = ExtractJsonString(actionJson, "resolution");
        std::string dispMode = ExtractJsonString(actionJson, "mode");
        int targetW = 1920, targetH = 1080;

        if (!resStr.empty()) {
            size_t xPos = resStr.find('x');
            if (xPos != std::string::npos) {
                targetW = std::stoi(resStr.substr(0, xPos));
                targetH = std::stoi(resStr.substr(xPos + 1));
            }
        }

        if (window && (!dispMode.empty() || !resStr.empty())) {
            std::string finalMode = dispMode.empty() ? (window->IsFullscreen() ? "fullscreen" : "windowed") : dispMode;
            window->SetDisplayMode(targetW, targetH, finalMode);
            int newW = window->GetClientWidth();
            int newH = window->GetClientHeight();
            if (renderer) renderer->RequestResize(newW, newH);
            Resize(newW, newH);
        }

        // 2. VSync
        std::string vsyncStr = ExtractJsonString(actionJson, "vsync");
        if (renderer && !vsyncStr.empty()) {
            renderer->SetVSyncEnabled(vsyncStr == "on" || vsyncStr == "true" || vsyncStr == "1");
        }

        // 3. FOV
        float fovVal = ExtractJsonFloat(actionJson, "fov", 0.0f);
        if (fovVal >= 45.0f && fovVal <= 130.0f && renderer) {
            renderer->SetFOV(fovVal);
        }

        // 4. Ray Tracing / Path Tracing
        std::string raytracing = ExtractJsonString(actionJson, "raytracingMode");
        if (renderer && !raytracing.empty()) {
            if (raytracing == "path_tracing" || raytracing == "path_tracing_overdrive") {
                renderer->SetPathTracingEnabled(true);
                renderer->m_rtShadowsEnabled = true;
                renderer->m_rtReflectionsEnabled = true;
            } else if (raytracing == "ray_tracing" || raytracing == "full_rt" || raytracing == "hybrid_rt") {
                renderer->SetPathTracingEnabled(false);
                renderer->m_rtShadowsEnabled = true;
                renderer->m_rtReflectionsEnabled = true;
            } else { // "off" / "rasterization"
                renderer->SetPathTracingEnabled(false);
                renderer->m_rtShadowsEnabled = false;
                renderer->m_rtReflectionsEnabled = false;
            }
        }

        // 5. Upscaler
        std::string upscaler = ExtractJsonString(actionJson, "upscaler");
        if (renderer && !upscaler.empty()) {
            if (upscaler == "dlss_4_5" || upscaler == "dlss") {
                if (renderer->IsDLSSSupported()) {
                    renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS);
                } else if (renderer->IsXeSSSupported()) {
                    renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS);
                } else {
                    renderer->SetUpscaleTechnique(UpscaleTechnique::FSR);
                }
            } else if (upscaler == "xess_2_0" || upscaler == "xess") {
                if (renderer->IsXeSSSupported()) {
                    renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS);
                } else {
                    renderer->SetUpscaleTechnique(UpscaleTechnique::FSR);
                }
            } else if (upscaler == "fsr_4_1_1" || upscaler == "fsr") {
                renderer->SetUpscaleTechnique(UpscaleTechnique::FSR);
            } else {
                renderer->SetUpscaleTechnique(UpscaleTechnique::None);
            }
        }

        // 6. Upscale Mode
        std::string upscaleMode = ExtractJsonString(actionJson, "upscaleMode");
        if (renderer && !upscaleMode.empty()) {
            if (upscaleMode == "quality") renderer->SetUpscaleQuality(UpscaleQuality::Quality);
            else if (upscaleMode == "balanced") renderer->SetUpscaleQuality(UpscaleQuality::Balanced);
            else if (upscaleMode == "performance") renderer->SetUpscaleQuality(UpscaleQuality::Performance);
            else if (upscaleMode == "ultra_performance") renderer->SetUpscaleQuality(UpscaleQuality::UltraPerformance);
            else if (upscaleMode == "dlaa") renderer->SetUpscaleQuality(UpscaleQuality::Native);
        }

        // 6b. DLSS Ray Reconstruction 2.0 (Next-Gen Neural Transformer Model)
        std::string rrStr = ExtractJsonString(actionJson, "rayReconstruction");
        if (rrStr.empty()) rrStr = ExtractJsonString(actionJson, "dlssRayReconstruction");
        if (renderer && !rrStr.empty()) {
            if (rrStr == "v2" || rrStr == "on" || rrStr == "true" || rrStr == "1") {
                renderer->SetDLSSRayReconstructionVersion(Renderer::DLSSRRVersion::Version2_0);
            } else if (rrStr == "v1") {
                renderer->SetDLSSRayReconstructionVersion(Renderer::DLSSRRVersion::Version1_0);
            } else {
                renderer->SetDLSSRayReconstructionVersion(Renderer::DLSSRRVersion::Off);
            }
        }

        // 6c. DLSS Multi-Frame Generation (MFG 3x/4x & FG 2x) & AFMF 2
        std::string fgStr = ExtractJsonString(actionJson, "mfg");
        if (fgStr.empty()) fgStr = ExtractJsonString(actionJson, "frameGen");
        if (renderer && !fgStr.empty()) {
            if (fgStr == "dlss_mfg_4x") {
                renderer->SetDLSSGActive(true);
                renderer->SetDLSSGMultiplier(4);
                renderer->SetFSRFrameGenEnabled(false);
            } else if (fgStr == "dlss_mfg_3x") {
                renderer->SetDLSSGActive(true);
                renderer->SetDLSSGMultiplier(3);
                renderer->SetFSRFrameGenEnabled(false);
            } else if (fgStr == "dlss_fg_2x" || fgStr == "dlss_fg" || fgStr == "dlss") {
                renderer->SetDLSSGActive(true);
                renderer->SetDLSSGMultiplier(2);
                renderer->SetFSRFrameGenEnabled(false);
            } else if (fgStr == "amd_afmf_2" || fgStr == "fsr_fg") {
                renderer->SetDLSSGActive(false);
                renderer->SetFSRFrameGenEnabled(true);
                renderer->SetFSRFrameGenMultiplier(2);
            } else { // off
                renderer->SetDLSSGActive(false);
                renderer->SetFSRFrameGenEnabled(false);
            }
        }

        // 7. Volumetric Fog
        std::string fogStr = ExtractJsonString(actionJson, "volumetricFog");
        if (renderer && !fogStr.empty()) {
            if (fogStr == "off" || fogStr == "false" || fogStr == "0") {
                renderer->SetVolumetricFogEnabled(false);
            } else {
                renderer->SetVolumetricFogEnabled(true);
                if (fogStr == "ultra") renderer->SetVolumetricFogDensity(0.045f);
                else if (fogStr == "high") renderer->SetVolumetricFogDensity(0.030f);
                else if (fogStr == "medium") renderer->SetVolumetricFogDensity(0.018f);
            }
        }

        // 8. Audio Master Volume
        float masterVol = ExtractJsonFloat(actionJson, "master", -1.0f);
        if (masterVol >= 0.0f) {
            EngineAudio::DualSenseAudioSystem::Get().SetMasterVolume((std::clamp)(masterVol / 100.0f, 0.0f, 1.0f));
        }
    }

    // 4. Cloth Physics Actions
    else if (action == "SPAWN_CLOTH") {
        std::string preset = ExtractJsonString(actionJson, "preset");
        if (preset == "Flag") EnginePhysics::ClothSystem::Get().SpawnFlag(spawnTarget, 2.6f, 1.8f, 24, 32, renderer);
        else if (preset == "Curtain") EnginePhysics::ClothSystem::Get().SpawnCurtain(spawnTarget, 3.0f, 2.5f, 28, 32, renderer);
        else if (preset == "Tablecloth") EnginePhysics::ClothSystem::Get().SpawnTablecloth(spawnTarget, 2.6f, 28, renderer);
        else if (preset == "Cape") EnginePhysics::ClothSystem::Get().SpawnCape(spawnTarget, 1.4f, 2.2f, 25, 20, renderer);
        else if (preset == "Trampoline") EnginePhysics::ClothSystem::Get().SpawnTrampoline(spawnTarget, 3.2f, 26, renderer);
        else if (preset == "FreeSheet") EnginePhysics::ClothSystem::Get().SpawnFreeSheet(spawnTarget, 2.5f, 2.5f, 24, 24, renderer);
        else EnginePhysics::ClothSystem::Get().SpawnFlag(spawnTarget, 2.4f, 1.6f, 24, 30, renderer);
    } else if (action == "RESET_CLOTH") {
        EnginePhysics::ClothSystem::Get().ResetAllCloths();
    } else if (action == "CLEAR_CLOTH") {
        EnginePhysics::ClothSystem::Get().ClearAllCloths(renderer);
    } else if (action == "SET_WIND") {
        float speed = ExtractJsonFloat(actionJson, "speed", 8.5f);
        EnginePhysics::ClothSystem::Get().SetWindSpeed(speed);
    }

    // 5. Hair Physics Actions
    else if (action == "SPAWN_HAIR") {
        std::string preset = ExtractJsonString(actionJson, "preset");
        EnginePhysics::HairHairstyleType hType = EnginePhysics::HairHairstyleType::LongFlowing;
        if (preset == "LongFlowing") hType = EnginePhysics::HairHairstyleType::LongFlowing;
        else if (preset == "Ponytail") hType = EnginePhysics::HairHairstyleType::Ponytail;
        else if (preset == "BobCut") hType = EnginePhysics::HairHairstyleType::BobCut;
        else if (preset == "TwinTails") hType = EnginePhysics::HairHairstyleType::TwinTails;
        else if (preset == "CurlyWavy") hType = EnginePhysics::HairHairstyleType::CurlyWavy;
        else if (preset == "FurBall") hType = EnginePhysics::HairHairstyleType::FurBall;

        EnginePhysics::HairSystem::Get().SpawnHairstyle(hType, spawnTarget, renderer);
    } else if (action == "SET_HAIR_COLOR") {
        int palette = ExtractJsonInt(actionJson, "palette", 0);
        auto hair = EnginePhysics::HairSystem::Get().GetSelectedHair();
        if (hair) hair->ApplyColorPreset(palette);
    } else if (action == "RESET_HAIR") {
        EnginePhysics::HairSystem::Get().ResetAll();
    } else if (action == "CLEAR_HAIR") {
        EnginePhysics::HairSystem::Get().ClearAll(renderer);
    }

    // 6. Graphics & Atmosphere
    else if (action == "SET_RAYTRACING") {
        bool pt = ExtractJsonBool(actionJson, "pathTracing", false);
        if (renderer) renderer->SetPathTracingEnabled(pt);
    } else if (action == "SET_RT_SHADOWS") {
        bool enabled = ExtractJsonBool(actionJson, "enabled", true);
        if (renderer) {
            renderer->m_rtShadowsEnabled = enabled;
            renderer->m_rtReflectionsEnabled = enabled;
        }
    } else if (action == "SET_UPSCALING") {
        std::string tech = ExtractJsonString(actionJson, "technique");
        if (renderer) {
            if (tech == "DLSS") renderer->SetUpscaleTechnique(UpscaleTechnique::DLSS);
            else if (tech == "XeSS" || tech == "XESS" || tech == "xess") renderer->SetUpscaleTechnique(UpscaleTechnique::XeSS);
            else if (tech == "FSR") renderer->SetUpscaleTechnique(UpscaleTechnique::FSR);
            else renderer->SetUpscaleTechnique(UpscaleTechnique::None);
        }
    } else if (action == "SET_RAIN") {
        bool enabled = ExtractJsonBool(actionJson, "enabled", false);
        EngineRain::RainSystem::Get().GetSettings().enabled = enabled;
    } else if (action == "SET_FOG") {
        bool enabled = ExtractJsonBool(actionJson, "enabled", true);
        if (renderer) renderer->SetVolumetricFogEnabled(enabled);
    }

    // 7. PhysX Sandbox
    else if (action == "SPAWN_TOWER") {
        if (renderer) EnginePhysics::PhysicsSystem::Get().SpawnTower(renderer, spawnTarget);
    } else if (action == "SHOOT_PROJECTILE") {
        if (renderer && camera) EnginePhysics::PhysicsSystem::Get().ShootProjectile(renderer, camPos, camFwd);
    } else if (action == "EXPLOSION") {
        DirectX::XMFLOAT3 expPos = { camPos.x + camFwd.x * 5.0f, camPos.y + camFwd.y * 5.0f, camPos.z + camFwd.z * 5.0f };
        EnginePhysics::PhysicsSystem::Get().AddExplosion(expPos, 15.0f, 2500.0f);
        for (auto& c : EnginePhysics::ClothSystem::Get().GetCloths()) {
            c->AddExplosionForce(expPos, 15.0f, 40.0f);
        }
    }

    // 8. Multiplayer Actions
    else if (action == "CONNECT_SERVER" || action == "multiplayer_connect") {
        std::string ip = ExtractJsonString(actionJson, "ip");
        int port = ExtractJsonInt(actionJson, "port", 7777);
        std::string name = ExtractJsonString(actionJson, "name");
        if (name.empty()) name = ExtractJsonString(actionJson, "nickname");
        if (ip.empty()) ip = "127.0.0.1";
        if (name.empty()) name = "Player";
        EngineNetwork::MultiplayerManager::Get().ConnectToServer(ip, static_cast<uint16_t>(port), name);
    } else if (action == "DISCONNECT_SERVER") {
        EngineNetwork::MultiplayerManager::Get().Disconnect();
    }

    // 9. Runtime Map / Level Loading
    else if (action == "LOAD_MAP" || action == "load_map" || action == "LOAD_LEVEL" || action == "load_level") {
        std::string mapName = ExtractJsonString(actionJson, "map");
        if (mapName.empty()) mapName = ExtractJsonString(actionJson, "level");
        if (mapName.empty()) mapName = ExtractJsonString(actionJson, "name");
        if (!mapName.empty()) {
            EngineLevel::LevelSystem::Get().RequestLoadLevel(mapName);
            SetMenuOpen(false, window);
        }
    } else if (action == "CYCLE_MAP" || action == "cycle_map") {
        EngineLevel::LevelSystem::Get().CycleToNextLevel(renderer, camera);
    }
}

} // namespace EngineUI
