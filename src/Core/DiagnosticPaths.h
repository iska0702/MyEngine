#pragma once
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace EngineDiagnostics {
inline const std::filesystem::path& Directory() {
    static const auto directory = [] {
        std::filesystem::path base;
#if defined(_WIN32)
        wchar_t local[32768]{};
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (length && length < 32768) base = local;
#endif
        if (base.empty()) base = std::filesystem::temp_directory_path();
#if defined(ENGINE_EDITOR)
        auto result = base / "Idontknow" / "Diagnostics" / "Editor";
#else
        auto result = base / "Idontknow" / "Diagnostics" / "Release";
#endif
        std::error_code error;
        std::filesystem::create_directories(result, error);
        return result;
    }();
    return directory;
}
}
