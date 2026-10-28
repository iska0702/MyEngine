#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <atomic>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace EngineLog {
inline std::atomic<bool> closeRequested{false};
inline bool Verbose() {
#if defined(_WIN32)
    static bool enabled=GetEnvironmentVariableW(L"MYENGINE_RENDER_TRACE",nullptr,0)>0;
    return enabled;
#else
    return false;
#endif
}
inline bool ShouldLog(const char* expression) {
    if (Verbose()) return true;
    if (std::strstr(expression,"Error") || std::strstr(expression,"error") ||
        std::strstr(expression,"FAILED") || std::strstr(expression,"failed") || std::strstr(expression,"CRITICAL")) return true;
    return !(std::strstr(expression,"\"Render:") == expression ||
        std::strstr(expression,"\"Skybox:") == expression ||
        std::strstr(expression,"\"PostProcess:") == expression ||
        std::strstr(expression,"\"Present:") == expression ||
        std::strstr(expression,"\"WFPF:") == expression);
}
inline void Write(const std::string& message) {
#if defined(_WIN32)
    OutputDebugStringA((message+"\n").c_str());
#endif
#if defined(ENGINE_EDITOR) || !defined(_WIN32)
    std::clog << message << '\n';
#endif
}
}
